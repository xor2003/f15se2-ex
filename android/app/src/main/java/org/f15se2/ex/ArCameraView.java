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
import android.util.Log;
import android.util.Size;
import android.util.SizeF;
import android.view.Display;
import android.view.Surface;
import android.view.TextureView;
import java.util.Collections;
import java.util.Locale;

/**
 * Camera2 preview aligned to the game's pitch and roll.
 *
 * This is intentionally not positional AR: the camera is only a replacement for
 * the GLES sky, and terrain remains the game's world.
 */
public final class ArCameraView extends TextureView
        implements TextureView.SurfaceTextureListener, SensorEventListener {
    private static final float ATTITUDE_FILTER = 0.18f;
    private static final long ATTITUDE_TRACE_INTERVAL_NS = 100_000_000L;
    private static final float CAMERA_OVERSCAN = 1.42f;
    private static final float MAX_PITCH_CORRECTION =
        (float)Math.toRadians(70.0);

    private final CameraManager cameraManager;
    private final SensorManager sensorManager;
    private final Sensor rotationSensor;
    private final float[] rotationMatrix = new float[9];
    private final float[] adjustedMatrix = new float[9];
    private final float[] attitudeOriginMatrix = new float[9];
    private final float[] relativeMatrix = new float[9];
    private final float[] relativeOrientation = new float[3];
    private final float[] gameAttitude = new float[2];

    private HandlerThread cameraThread;
    private Handler cameraHandler;
    private CameraDevice cameraDevice;
    private CameraCaptureSession captureSession;
    private boolean resumed;
    private boolean attitudeInitialized;
    private float deviceYawOffset;
    private float devicePitchOffset;
    private float deviceRollOffset;
    private long lastAttitudeTraceNs;
    private float verticalFov = (float)Math.toRadians(50.0);

    private static native void nativeSetCameraReady(boolean ready);
    private static native void nativeSetDeviceAttitude(float yaw, float pitch, float roll);
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
        setDeviceAttitude(0.0f, 0.0f, 0.0f);
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

    private static void setDeviceAttitude(float yaw, float pitch, float roll) {
        try {
            nativeSetDeviceAttitude(yaw, pitch, roll);
        } catch (UnsatisfiedLinkError ignored) {
            // SDL has not loaded the application library yet.
        }
    }

    private static float filteredAngle(float current, float target, float amount) {
        float delta = target - current;
        while (delta > Math.PI) delta -= (float)(2.0 * Math.PI);
        while (delta < -Math.PI) delta += (float)(2.0 * Math.PI);
        return current + delta * amount;
    }

    /** Keeps Camera2 as a stable full-screen layer behind transparent game sky. */
    private void alignPreview() {
        setRotation(0.0f);
        setTranslationX(0.0f);
        setTranslationY(0.0f);
        setScaleX(1.0f);
        setScaleY(1.0f);
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
        if (!attitudeInitialized) {
            System.arraycopy(adjustedMatrix, 0, attitudeOriginMatrix, 0,
                             adjustedMatrix.length);
            deviceYawOffset = 0.0f;
            devicePitchOffset = 0.0f;
            deviceRollOffset = 0.0f;
            attitudeInitialized = true;
        } else {
            /*
             * Derive attitude from the rotation relative to the initial handset
             * pose. Subtracting absolute Euler angles is singular when a phone
             * is held upright and makes pitch/roll exchange or jump.
             */
            for (int row = 0; row < 3; row++) {
                for (int column = 0; column < 3; column++) {
                    float value = 0.0f;
                    for (int k = 0; k < 3; k++) {
                        value += attitudeOriginMatrix[k * 3 + row] *
                                 adjustedMatrix[k * 3 + column];
                    }
                    relativeMatrix[row * 3 + column] = value;
                }
            }
            SensorManager.getOrientation(relativeMatrix, relativeOrientation);
            deviceYawOffset = filteredAngle(
                deviceYawOffset, relativeOrientation[0], ATTITUDE_FILTER);
            devicePitchOffset = filteredAngle(
                devicePitchOffset, relativeOrientation[1], ATTITUDE_FILTER);
            deviceRollOffset = filteredAngle(
                deviceRollOffset, relativeOrientation[2], ATTITUDE_FILTER);
        }
        setDeviceAttitude(deviceYawOffset, devicePitchOffset, deviceRollOffset);
        /*
         * Opt-in sensor telemetry for diagnosing device-specific axis mapping.
         * Enable with: adb shell setprop log.tag.F15AR DEBUG
         */
        if (Log.isLoggable("F15AR", Log.DEBUG) &&
            event.timestamp - lastAttitudeTraceNs >= ATTITUDE_TRACE_INTERVAL_NS) {
            lastAttitudeTraceNs = event.timestamp;
            Log.d("F15AR", String.format(
                Locale.US,
                "sensor ns=%d display=%d yaw=%+.5f pitch=%+.5f roll=%+.5f",
                event.timestamp, rotation, deviceYawOffset,
                devicePitchOffset, deviceRollOffset));
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
