package org.f15se2.ex;

import android.Manifest;
import android.annotation.SuppressLint;
import android.content.Context;
import android.content.pm.PackageManager;
import android.graphics.Matrix;
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

    private final CameraManager cameraManager;
    private final SensorManager sensorManager;
    private final Sensor rotationSensor;
    private final float[] rotationMatrix = new float[9];
    private final float[] adjustedMatrix = new float[9];
    private final float[] attitudeOriginMatrix = new float[9];
    private final float[] relativeMatrix = new float[9];
    private final float[] relativeQuaternion = new float[4];
    private final float[] attitudeOriginGravity = new float[3];

    private HandlerThread cameraThread;
    private Handler cameraHandler;
    private CameraDevice cameraDevice;
    private CameraCaptureSession captureSession;
    private Size previewSize;
    private boolean resumed;
    private boolean attitudeInitialized;
    private float deviceYawOffset;
    private float devicePitchOffset;
    private float deviceRollOffset;
    private long lastAttitudeTraceNs;

    private static native void nativeSetCameraReady(boolean ready);
    private static native void nativeSetDeviceAttitude(float yaw, float pitch, float roll);

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
            previewSize = choosePreviewSize(selected);
            texture.setDefaultBufferSize(
                previewSize.getWidth(), previewSize.getHeight());
            alignPreview();
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

    /**
     * Converts a row-major rotation matrix to a normalized w,x,y,z quaternion.
     *
     * Quaternion decomposition avoids the branch singularities produced by
     * SensorManager.getOrientation() when the handset approaches vertical.
     */
    private static void matrixToQuaternion(float[] matrix, float[] quaternion) {
        float trace = matrix[0] + matrix[4] + matrix[8];
        float w;
        float x;
        float y;
        float z;

        if (trace > 0.0f) {
            float scale = 2.0f * (float)Math.sqrt(trace + 1.0f);
            w = 0.25f * scale;
            x = (matrix[7] - matrix[5]) / scale;
            y = (matrix[2] - matrix[6]) / scale;
            z = (matrix[3] - matrix[1]) / scale;
        } else if (matrix[0] > matrix[4] && matrix[0] > matrix[8]) {
            float scale = 2.0f *
                (float)Math.sqrt(1.0f + matrix[0] - matrix[4] - matrix[8]);
            w = (matrix[7] - matrix[5]) / scale;
            x = 0.25f * scale;
            y = (matrix[1] + matrix[3]) / scale;
            z = (matrix[2] + matrix[6]) / scale;
        } else if (matrix[4] > matrix[8]) {
            float scale = 2.0f *
                (float)Math.sqrt(1.0f + matrix[4] - matrix[0] - matrix[8]);
            w = (matrix[2] - matrix[6]) / scale;
            x = (matrix[1] + matrix[3]) / scale;
            y = 0.25f * scale;
            z = (matrix[5] + matrix[7]) / scale;
        } else {
            float scale = 2.0f *
                (float)Math.sqrt(1.0f + matrix[8] - matrix[0] - matrix[4]);
            w = (matrix[3] - matrix[1]) / scale;
            x = (matrix[2] + matrix[6]) / scale;
            y = (matrix[5] + matrix[7]) / scale;
            z = 0.25f * scale;
        }

        float length = (float)Math.sqrt(w * w + x * x + y * y + z * z);
        if (length <= 1.0e-6f) {
            quaternion[0] = 1.0f;
            quaternion[1] = 0.0f;
            quaternion[2] = 0.0f;
            quaternion[3] = 0.0f;
            return;
        }
        quaternion[0] = w / length;
        quaternion[1] = x / length;
        quaternion[2] = y / length;
        quaternion[3] = z / length;
    }

    /**
     * Separates look-around yaw from handset tilt and updates filtered controls.
     *
     * The twist is rotation around the screen normal. Removing it leaves a
     * swing quaternion whose X/Y rotation vector controls pitch and roll
     * without yaw leaking into either flight axis.
     */
    private void updateRelativeAttitude() {
        matrixToQuaternion(relativeMatrix, relativeQuaternion);
        float w = relativeQuaternion[0];
        float x = relativeQuaternion[1];
        float y = relativeQuaternion[2];
        float z = relativeQuaternion[3];

        float twistLength = (float)Math.sqrt(w * w + z * z);
        float twistW = 1.0f;
        float twistZ = 0.0f;
        if (twistLength > 1.0e-6f) {
            twistW = w / twistLength;
            twistZ = z / twistLength;
        }

        float yawTarget = -2.0f * (float)Math.atan2(twistZ, twistW);
        deviceYawOffset = filteredAngle(
            deviceYawOffset, yawTarget, ATTITUDE_FILTER);

        /*
         * The compass can be disturbed by nearby magnets. Derive flight tilt
         * only from gravity: rotating the handset around the real vertical
         * leaves this vector unchanged and therefore cannot bank the aircraft.
         */
        float gravityX = adjustedMatrix[6];
        float gravityY = adjustedMatrix[7];
        float gravityZ = adjustedMatrix[8];
        float gravityLength = (float)Math.sqrt(
            gravityX * gravityX + gravityY * gravityY + gravityZ * gravityZ);
        if (gravityLength <= 1.0e-6f) {
            return;
        }
        gravityX /= gravityLength;
        gravityY /= gravityLength;
        gravityZ /= gravityLength;

        float originX = attitudeOriginGravity[0];
        float originY = attitudeOriginGravity[1];
        float originZ = attitudeOriginGravity[2];
        float dot = originX * gravityX + originY * gravityY +
                    originZ * gravityZ;
        dot = Math.max(-1.0f, Math.min(1.0f, dot));
        float swingW = (float)Math.sqrt(Math.max(0.0f, (1.0f + dot) * 0.5f));
        float swingX;
        float swingY;
        float swingZ;
        if (swingW > 1.0e-6f) {
            float scale = 0.5f / swingW;
            swingX = (originY * gravityZ - originZ * gravityY) * scale;
            swingY = (originZ * gravityX - originX * gravityZ) * scale;
            swingZ = (originX * gravityY - originY * gravityX) * scale;
        } else {
            /* A flipped handset has no unique shortest tilt axis. */
            swingX = 1.0f;
            swingY = 0.0f;
            swingZ = 0.0f;
        }

        float vectorLength = (float)Math.sqrt(
            swingX * swingX + swingY * swingY + swingZ * swingZ);
        float vectorScale = 2.0f;
        if (vectorLength > 1.0e-6f) {
            vectorScale =
                2.0f * (float)Math.atan2(vectorLength, swingW) / vectorLength;
        }

        /*
         * Gravity swing has the opposite vector direction from the device-to-
         * world attitude quaternion. These signs preserve the established
         * physical pitch/roll directions while rejecting compass rotation.
         */
        float pitchTarget = swingX * vectorScale;
        float rollTarget = -swingY * vectorScale;
        devicePitchOffset +=
            (pitchTarget - devicePitchOffset) * ATTITUDE_FILTER;
        deviceRollOffset +=
            (rollTarget - deviceRollOffset) * ATTITUDE_FILTER;
    }

    /**
     * Center-crops Camera2 without changing its aspect ratio.
     *
     * This transform depends only on view and preview dimensions. Device and
     * game attitude must never rotate, translate or stretch the real image.
     */
    private void alignPreview() {
        int viewWidth = getWidth();
        int viewHeight = getHeight();
        Matrix transform = new Matrix();
        if (previewSize != null && viewWidth > 0 && viewHeight > 0) {
            float fillScale = Math.max(
                viewWidth / (float)previewSize.getWidth(),
                viewHeight / (float)previewSize.getHeight());
            float scaleX =
                previewSize.getWidth() * fillScale / (float)viewWidth;
            float scaleY =
                previewSize.getHeight() * fillScale / (float)viewHeight;
            transform.setScale(
                scaleX, scaleY, viewWidth * 0.5f, viewHeight * 0.5f);
        }
        setTransform(transform);
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
            attitudeOriginGravity[0] = adjustedMatrix[6];
            attitudeOriginGravity[1] = adjustedMatrix[7];
            attitudeOriginGravity[2] = adjustedMatrix[8];
            float gravityLength = (float)Math.sqrt(
                attitudeOriginGravity[0] * attitudeOriginGravity[0] +
                attitudeOriginGravity[1] * attitudeOriginGravity[1] +
                attitudeOriginGravity[2] * attitudeOriginGravity[2]);
            if (gravityLength > 1.0e-6f) {
                attitudeOriginGravity[0] /= gravityLength;
                attitudeOriginGravity[1] /= gravityLength;
                attitudeOriginGravity[2] /= gravityLength;
            }
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
            updateRelativeAttitude();
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
