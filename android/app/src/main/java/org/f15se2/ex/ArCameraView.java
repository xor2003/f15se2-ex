package org.f15se2.ex;

import android.Manifest;
import android.annotation.SuppressLint;
import android.content.Context;
import android.content.pm.PackageManager;
import android.graphics.SurfaceTexture;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CaptureRequest;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Size;
import android.util.SizeF;
import android.view.Display;
import android.view.Surface;
import android.view.TextureView;
import java.util.Collections;

/**
 * Camera2 preview aligned to the game's pitch and roll.
 *
 * This is intentionally not positional AR: the camera is only a replacement for
 * the GLES sky, and terrain remains the game's world.
 */
public final class ArCameraView extends TextureView
        implements TextureView.SurfaceTextureListener, SensorEventListener {
    private static final float ATTITUDE_FILTER = 0.18f;
    private static final float CAMERA_OVERSCAN = 1.42f;
    private static final float MAX_PITCH_CORRECTION =
        (float)Math.toRadians(70.0);

    private final CameraManager cameraManager;
    private final SensorManager sensorManager;
    private final Sensor rotationSensor;
    private final float[] rotationMatrix = new float[9];
    private final float[] adjustedMatrix = new float[9];
    private final float[] orientation = new float[3];
    private final float[] gameAttitude = new float[2];

    private HandlerThread cameraThread;
    private Handler cameraHandler;
    private CameraDevice cameraDevice;
    private CameraCaptureSession captureSession;
    private boolean resumed;
    private boolean attitudeInitialized;
    private float devicePitch;
    private float deviceRoll;
    private float verticalFov = (float)Math.toRadians(50.0);

    private static native void nativeSetCameraReady(boolean ready);
    private static native void nativeGetGameAttitude(float[] attitude);

    public ArCameraView(Context context) {
        super(context);
        cameraManager = (CameraManager)context.getSystemService(Context.CAMERA_SERVICE);
        sensorManager = (SensorManager)context.getSystemService(Context.SENSOR_SERVICE);
        rotationSensor = sensorManager.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR);
        setSurfaceTextureListener(this);
        setOpaque(false);
    }

    /** Starts camera and attitude updates while the activity is visible. */
    public void resume() {
        if (resumed) {
            return;
        }
        resumed = true;
        cameraThread = new HandlerThread("f15-ar-camera");
        cameraThread.start();
        cameraHandler = new Handler(cameraThread.getLooper());
        if (rotationSensor != null) {
            sensorManager.registerListener(this, rotationSensor,
                                           SensorManager.SENSOR_DELAY_GAME);
        }
        if (isAvailable()) {
            openCamera(getSurfaceTexture());
        }
    }

    /** Releases all camera and sensor resources before SDL loses its surface. */
    public void pause() {
        resumed = false;
        sensorManager.unregisterListener(this);
        closeCamera();
        if (cameraThread != null) {
            cameraThread.quitSafely();
            cameraThread = null;
            cameraHandler = null;
        }
        attitudeInitialized = false;
    }

    /** Selects a back camera and starts a preview into this TextureView. */
    @SuppressLint("MissingPermission")
    private void openCamera(SurfaceTexture texture) {
        if (!resumed || texture == null ||
            getContext().checkSelfPermission(Manifest.permission.CAMERA) !=
            PackageManager.PERMISSION_GRANTED) {
            setCameraReady(false);
            return;
        }
        try {
            String selectedId = null;
            CameraCharacteristics selected = null;
            for (String id : cameraManager.getCameraIdList()) {
                CameraCharacteristics characteristics =
                    cameraManager.getCameraCharacteristics(id);
                Integer facing = characteristics.get(CameraCharacteristics.LENS_FACING);
                if (selectedId == null ||
                    (facing != null &&
                     facing == CameraCharacteristics.LENS_FACING_BACK)) {
                    selectedId = id;
                    selected = characteristics;
                }
                if (facing != null &&
                    facing == CameraCharacteristics.LENS_FACING_BACK) {
                    break;
                }
            }
            if (selectedId == null || selected == null) {
                setCameraReady(false);
                return;
            }
            updateVerticalFov(selected);
            Size preview = choosePreviewSize(selected);
            texture.setDefaultBufferSize(preview.getWidth(), preview.getHeight());
            cameraManager.openCamera(selectedId, cameraStateCallback, cameraHandler);
        } catch (CameraAccessException | SecurityException error) {
            setCameraReady(false);
        }
    }

    /** Uses a moderate preview size to avoid wasting bandwidth behind low-poly 3D. */
    private Size choosePreviewSize(CameraCharacteristics characteristics) {
        android.hardware.camera2.params.StreamConfigurationMap map =
            characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
        Size fallback = new Size(1280, 720);
        if (map == null) {
            return fallback;
        }
        Size[] sizes = map.getOutputSizes(SurfaceTexture.class);
        if (sizes == null || sizes.length == 0) {
            return fallback;
        }
        Size best = sizes[0];
        long bestScore = Long.MAX_VALUE;
        for (Size size : sizes) {
            long area = (long)size.getWidth() * size.getHeight();
            long score = Math.abs(area - 1280L * 720L);
            if (score < bestScore) {
                best = size;
                bestScore = score;
            }
        }
        return best;
    }

    /** Derives focal length in view pixels from physical camera calibration. */
    private void updateVerticalFov(CameraCharacteristics characteristics) {
        SizeF sensorSize =
            characteristics.get(CameraCharacteristics.SENSOR_INFO_PHYSICAL_SIZE);
        float[] focalLengths =
            characteristics.get(CameraCharacteristics.LENS_INFO_AVAILABLE_FOCAL_LENGTHS);
        if (sensorSize != null && focalLengths != null && focalLengths.length > 0) {
            verticalFov = 2.0f * (float)Math.atan(
                sensorSize.getHeight() / (2.0f * focalLengths[0]));
        }
    }

    private final CameraDevice.StateCallback cameraStateCallback =
        new CameraDevice.StateCallback() {
            @Override
            public void onOpened(CameraDevice camera) {
                cameraDevice = camera;
                createPreviewSession();
            }

            @Override
            public void onDisconnected(CameraDevice camera) {
                camera.close();
                cameraDevice = null;
                setCameraReady(false);
            }

            @Override
            public void onError(CameraDevice camera, int error) {
                camera.close();
                cameraDevice = null;
                setCameraReady(false);
            }
        };

    /** Connects the Camera2 repeating preview request to the TextureView surface. */
    private void createPreviewSession() {
        SurfaceTexture texture = getSurfaceTexture();
        if (!resumed || cameraDevice == null || texture == null) {
            setCameraReady(false);
            return;
        }
        Surface surface = new Surface(texture);
        try {
            CaptureRequest.Builder request =
                cameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW);
            request.addTarget(surface);
            cameraDevice.createCaptureSession(Collections.singletonList(surface),
                new CameraCaptureSession.StateCallback() {
                    @Override
                    public void onConfigured(CameraCaptureSession session) {
                        if (!resumed || cameraDevice == null) {
                            session.close();
                            return;
                        }
                        captureSession = session;
                        try {
                            session.setRepeatingRequest(request.build(), null,
                                                        cameraHandler);
                            setCameraReady(true);
                        } catch (CameraAccessException error) {
                            setCameraReady(false);
                        }
                    }

                    @Override
                    public void onConfigureFailed(CameraCaptureSession session) {
                        setCameraReady(false);
                    }
                }, cameraHandler);
        } catch (CameraAccessException error) {
            surface.release();
            setCameraReady(false);
        }
    }

    /** Closes Camera2 objects in dependency order and restores the normal sky. */
    private void closeCamera() {
        setCameraReady(false);
        if (captureSession != null) {
            captureSession.close();
            captureSession = null;
        }
        if (cameraDevice != null) {
            cameraDevice.close();
            cameraDevice = null;
        }
    }

    private static void setCameraReady(boolean ready) {
        try {
            nativeSetCameraReady(ready);
        } catch (UnsatisfiedLinkError ignored) {
            // SDL has not loaded the application library yet; normal sky remains.
        }
    }

    private static float filteredAngle(float current, float target, float amount) {
        float delta = target - current;
        while (delta > Math.PI) delta -= (float)(2.0 * Math.PI);
        while (delta < -Math.PI) delta += (float)(2.0 * Math.PI);
        return current + delta * amount;
    }

    /** Applies game-device pitch/roll difference to the camera preview only. */
    private void alignPreview() {
        try {
            nativeGetGameAttitude(gameAttitude);
        } catch (UnsatisfiedLinkError ignored) {
            return;
        }
        float rollCorrection = gameAttitude[1] - deviceRoll;
        float pitchCorrection = gameAttitude[0] - devicePitch;
        pitchCorrection = Math.max(-MAX_PITCH_CORRECTION,
                                   Math.min(MAX_PITCH_CORRECTION, pitchCorrection));
        float focalLengthPixels =
            getHeight() / (2.0f * (float)Math.tan(verticalFov / 2.0f));
        setRotation((float)Math.toDegrees(rollCorrection));
        setTranslationY((float)Math.tan(pitchCorrection) * focalLengthPixels);
        setScaleX(CAMERA_OVERSCAN);
        setScaleY(CAMERA_OVERSCAN);
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        SensorManager.getRotationMatrixFromVector(rotationMatrix, event.values);
        Display display = getDisplay();
        int rotation = display != null ? display.getRotation() : Surface.ROTATION_0;
        int axisX = SensorManager.AXIS_X;
        int axisY = SensorManager.AXIS_Y;
        if (rotation == Surface.ROTATION_90) {
            axisX = SensorManager.AXIS_Y;
            axisY = SensorManager.AXIS_MINUS_X;
        } else if (rotation == Surface.ROTATION_180) {
            axisX = SensorManager.AXIS_MINUS_X;
            axisY = SensorManager.AXIS_MINUS_Y;
        } else if (rotation == Surface.ROTATION_270) {
            axisX = SensorManager.AXIS_MINUS_Y;
            axisY = SensorManager.AXIS_X;
        }
        SensorManager.remapCoordinateSystem(rotationMatrix, axisX, axisY,
                                            adjustedMatrix);
        SensorManager.getOrientation(adjustedMatrix, orientation);
        if (!attitudeInitialized) {
            devicePitch = orientation[1];
            deviceRoll = orientation[2];
            attitudeInitialized = true;
        } else {
            devicePitch = filteredAngle(devicePitch, orientation[1], ATTITUDE_FILTER);
            deviceRoll = filteredAngle(deviceRoll, orientation[2], ATTITUDE_FILTER);
        }
        alignPreview();
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {
    }

    @Override
    public void onSurfaceTextureAvailable(SurfaceTexture surface, int width, int height) {
        openCamera(surface);
    }

    @Override
    public void onSurfaceTextureSizeChanged(SurfaceTexture surface, int width, int height) {
        alignPreview();
    }

    @Override
    public boolean onSurfaceTextureDestroyed(SurfaceTexture surface) {
        closeCamera();
        return true;
    }

    @Override
    public void onSurfaceTextureUpdated(SurfaceTexture surface) {
    }
}
