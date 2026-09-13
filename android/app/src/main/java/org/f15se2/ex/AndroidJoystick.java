package org.f15se2.ex;

import android.view.InputDevice;
import android.view.MotionEvent;

/** Identify the throttle and preserve its value before SDL's signed-16 narrowing. */
final class AndroidJoystick {
    private AndroidJoystick() {}

    private static int throttleDevice = -1;
    private static double latestThrottle = Double.NaN;

    // Motion events arrive on the UI thread; SDL queries on its native thread.
    static synchronized void captureThrottle(MotionEvent event) {
        if (event.getDeviceId() != throttleDevice ||
                !event.isFromSource(InputDevice.SOURCE_JOYSTICK) ||
                event.getActionMasked() != MotionEvent.ACTION_MOVE) return;
        InputDevice device = event.getDevice();
        InputDevice.MotionRange range = device == null ? null :
            device.getMotionRange(MotionEvent.AXIS_THROTTLE, event.getSource());
        if (range == null || range.getRange() <= 0) return;
        // Some TV firmware omits the raw minimum when scaling a unipolar axis.
        // Preserve out-of-range values: clamping would lose lever travel, while
        // SDL's Sint16 conversion wraps. Endpoint calibration removes the offset.
        double normalized = (event.getAxisValue(MotionEvent.AXIS_THROTTLE) -
                             (double)range.getMin()) / range.getRange();
        double sample = (normalized * 2.0 - 1.0) * 32767.0;
        latestThrottle = Double.isNaN(sample) || Double.isInfinite(sample) ?
            Double.NaN : sample;
    }

    static synchronized double throttleValue() {
        return throttleDevice >= 0 && InputDevice.getDevice(throttleDevice) != null ?
            latestThrottle : Double.NaN;
    }

    static synchronized int throttleAxis(int vendor, int product, String name, int axes) {
        throttleDevice = -1;
        latestThrottle = Double.NaN;
        int result = -1;
        int matchedDevice = -1;
        boolean found = false;
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device == null || device.getVendorId() != vendor ||
                    device.getProductId() != product || !device.getName().equals(name)) continue;
            int count = 0;
            int beforeThrottle = 0;
            boolean throttle = false;
            for (InputDevice.MotionRange range : device.getMotionRanges()) {
                if ((range.getSource() & InputDevice.SOURCE_CLASS_JOYSTICK) == 0) continue;
                int axis = range.getAxis();
                if (axis == MotionEvent.AXIS_HAT_X || axis == MotionEvent.AXIS_HAT_Y) continue;
                ++count;
                if (axis == MotionEvent.AXIS_THROTTLE) throttle = true;
                // SDL sorts analog ranges by axis code. Its Z/RX/RY and
                // GAS/BRAKE swaps never cross THROTTLE, so counting is enough.
                if (axis < MotionEvent.AXIS_THROTTLE) ++beforeThrottle;
            }
            if (count != axes) continue;
            int candidate = throttle ? beforeThrottle : -1;
            // SDL does not expose Android's device ID. Even identical layouts
            // are ambiguous when selecting whose live samples to use.
            if (found) return -1;
            found = true;
            result = candidate;
            matchedDevice = id;
        }
        if (result >= 2) throttleDevice = matchedDevice;
        return result;
    }
}
