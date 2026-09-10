package org.f15se2.ex;

import android.view.InputDevice;
import android.view.MotionEvent;

/** Translate named Android motion ranges to SDL's raw analog-axis index. */
final class AndroidJoystick {
    private AndroidJoystick() {}

    static int throttleAxis(int vendor, int product, String name, int axes) {
        int result = -1;
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
            // SDL does not expose Android's device ID. Do not guess if two
            // otherwise identical devices advertise different layouts.
            if (found && candidate != result) return -1;
            found = true;
            result = candidate;
        }
        return result;
    }
}
