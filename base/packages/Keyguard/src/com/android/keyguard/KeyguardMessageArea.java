/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.keyguard;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.animation.ObjectAnimator;
import android.content.ContentResolver;
import android.content.Context;
import android.os.BatteryManager;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.os.SystemClock;
import android.os.UserHandle;
import android.provider.Settings;
import android.text.TextUtils;
import android.util.AttributeSet;
import android.util.Slog;
import android.view.View;
import android.widget.TextView;

import libcore.util.MutableInt;

import java.lang.ref.WeakReference;

import com.android.internal.widget.LockPatternUtils;

/***
 * Manages a number of views inside of the given layout. See below for a list of widgets.
 */
class KeyguardMessageArea extends TextView {
    /** Handler token posted with accessibility announcement runnables. */
    private static final Object ANNOUNCE_TOKEN = new Object();

    /**
     * Delay before speaking an accessibility announcement. Used to prevent
     * lift-to-type from interrupting itself.
     */
    private static final long ANNOUNCEMENT_DELAY = 250;

    static final int CHARGING_ICON = 0; //R.drawable.ic_lock_idle_charging;
    static final int BATTERY_LOW_ICON = 0; //R.drawable.ic_lock_idle_low_battery;

    static final int SECURITY_MESSAGE_DURATION = 5000;
    /// M: ALPS00682491 fix animation done may clear next message, so disable the effect.
    protected static final int FADE_DURATION = 0;

    private static final String TAG = "KeyguardMessageArea";

    /// M: Support multiple batteries feature
    private static final int BATTERY_NUMBER = 2;

    // are we showing battery information?
    boolean mShowingBatteryInfo[];

    // is the bouncer up?
    boolean mShowingBouncer = false;

    // last known plugged in state
    boolean mCharging[];

    // last known battery level
    int mBatteryLevel[];

    KeyguardUpdateMonitor mUpdateMonitor;

    // Timeout before we reset the message to show charging/owner info
    long mTimeout = SECURITY_MESSAGE_DURATION;

    // Shadowed text values
    protected boolean mBatteryCharged[];
    protected boolean mBatteryIsLow[];

    private Handler mHandler;

    CharSequence mMessage;
    boolean mShowingMessage;
    private CharSequence mSeparator;
    private LockPatternUtils mLockPatternUtils;

    Runnable mClearMessageRunnable = new Runnable() {
        @Override
        public void run() {
            mMessage = null;
            mShowingMessage = false;
            if (mShowingBouncer) {
                hideMessage(FADE_DURATION, true);
            } else {
                update();
            }
        }
    };

    public static class Helper implements SecurityMessageDisplay {
        KeyguardMessageArea mMessageArea;
        Helper(View v) {
            mMessageArea = (KeyguardMessageArea) v.findViewById(R.id.keyguard_message_area);
            if (mMessageArea == null) {
                throw new RuntimeException("Can't find keyguard_message_area in " + v.getClass());
            }
        }

        public void setMessage(CharSequence msg, boolean important) {
            if (!TextUtils.isEmpty(msg) && important) {
                mMessageArea.mMessage = msg;
                mMessageArea.securityMessageChanged();
            }
        }

        public void setMessage(int resId, boolean important) {
            if (resId != 0 && important) {
                mMessageArea.mMessage = mMessageArea.getContext().getResources().getText(resId);
                mMessageArea.securityMessageChanged();
            }
        }

        public void setMessage(int resId, boolean important, Object... formatArgs) {
            if (resId != 0 && important) {
                mMessageArea.mMessage = mMessageArea.getContext().getString(resId, formatArgs);
                mMessageArea.securityMessageChanged();
            }
        }

        @Override
        public void showBouncer(int duration) {
            mMessageArea.hideMessage(duration, false);
            mMessageArea.mShowingBouncer = true;
        }

        @Override
        public void hideBouncer(int duration) {
            mMessageArea.showMessage(duration);
            mMessageArea.mShowingBouncer = false;
        }

        @Override
        public void setTimeout(int timeoutMs) {
            mMessageArea.mTimeout = timeoutMs;
        }
    }

    private KeyguardUpdateMonitorCallback mInfoCallback = new KeyguardUpdateMonitorCallback() {
        @Override
        public void onRefreshBatteryInfo(KeyguardUpdateMonitor.BatteryStatus status) {
            final int idx = status.index;
            mShowingBatteryInfo[idx] = status.isPluggedIn() || status.isBatteryLow();
            ///M: ALPS00602318, Charging status may be not updated immediately after plugged out charger
            mCharging[idx] = status.isPluggedIn() &&
                     (status.status == BatteryManager.BATTERY_STATUS_CHARGING
                     || status.status == BatteryManager.BATTERY_STATUS_FULL);
            mBatteryLevel[idx] = status.level;
            mBatteryCharged[idx] = status.isCharged();
            mBatteryIsLow[idx] = status.isBatteryLow();
            update();
        }
        public void onScreenTurnedOff(int why) {
            setSelected(false);
        };
        public void onScreenTurnedOn() {
            setSelected(true);
        };
    };

    public KeyguardMessageArea(Context context) {
        this(context, null);
    }

    public KeyguardMessageArea(Context context, AttributeSet attrs) {
        super(context, attrs);
        setLayerType(LAYER_TYPE_HARDWARE, null); // work around nested unclipped SaveLayer bug

        mLockPatternUtils = new LockPatternUtils(context);
        
        ///M: support multiple batteries
        mShowingBatteryInfo = new boolean[BATTERY_NUMBER];
        mCharging = new boolean[BATTERY_NUMBER];
        mBatteryCharged = new boolean[BATTERY_NUMBER];
        mBatteryIsLow = new boolean[BATTERY_NUMBER];
        mBatteryLevel = new int[BATTERY_NUMBER];
        for (int i = 0; i < BATTERY_NUMBER; i++) {
            mShowingBatteryInfo[i] = false;
            mCharging[i] = false;
            mBatteryCharged[i] = false;
            mBatteryIsLow[i] = false;
            mBatteryLevel[i] = 100;
        }


        // Registering this callback immediately updates the battery state, among other things.
        mUpdateMonitor = KeyguardUpdateMonitor.getInstance(getContext());
        mUpdateMonitor.registerCallback(mInfoCallback);
        mHandler = new Handler(Looper.myLooper());

        mSeparator = getResources().getString(R.string.kg_text_message_separator);

        update();
    }

    @Override
    protected void onFinishInflate() {
        final boolean screenOn = KeyguardUpdateMonitor.getInstance(mContext).isScreenOn();
        setSelected(screenOn); // This is required to ensure marquee works
    }

    public void securityMessageChanged() {
        setAlpha(1f);
        mShowingMessage = true;
        update();
        mHandler.removeCallbacks(mClearMessageRunnable);
        if (mTimeout > 0) {
            mHandler.postDelayed(mClearMessageRunnable, mTimeout);
        }
        mHandler.removeCallbacksAndMessages(ANNOUNCE_TOKEN);
        mHandler.postAtTime(new AnnounceRunnable(this, getText()), ANNOUNCE_TOKEN,
                (SystemClock.uptimeMillis() + ANNOUNCEMENT_DELAY));
    }

    /**
     * Update the status lines based on these rules:
     * AlarmStatus: Alarm state always gets it's own line.
     * Status1 is shared between help, battery status and generic unlock instructions,
     * prioritized in that order.
     * @param showStatusLines status lines are shown if true
     */
    void update() {

        CharSequence statusStr;
        CharSequence deviceStatus[] = new CharSequence[BATTERY_NUMBER];
        MutableInt icon = new MutableInt(0);

        if (mUpdateMonitor != null && mUpdateMonitor.isDocktoDesk()) {  // show 2 batteries information
            for (int i =0; i < BATTERY_NUMBER; i++) {
                CharSequence deviceName = null;
                CharSequence chargingStr = getChargeInfo(icon, i);
                switch (i) {
                    case 0: // Phone
                        deviceName = getContext().getString(
                                com.android.internal.R.string.default_audio_route_name);
                        break;
                    case 1: // SmartBook
                        deviceName = "SmartBook";
                        break;
                }
                deviceStatus[i] = chargingStr != null ? (deviceName.toString()+" "+chargingStr.toString()) : null;
            }
            statusStr = deviceStatus[0] != null ? concat(deviceStatus[0],deviceStatus[1]): null;
            statusStr = concat(statusStr, getOwnerInfo(), getCurrentMessage());

        } else {
            statusStr = concat(getChargeInfo(icon, 0), getOwnerInfo(), getCurrentMessage());
            setCompoundDrawablesWithIntrinsicBounds(icon.value, 0, 0, 0);
        }

        Slog.v(TAG, "statusStr="+statusStr.toString());

        /// M: If dm lock or PPL Lock is on, we should tell user here @{
        if (AntiTheftManager.isKeyguardCurrentModeAntiTheftMode()) {
            setTextMediatek(statusStr);
        } else {
            setText(statusStr);
        } 
        /// @}
    }

    private CharSequence concat(CharSequence... args) {
        StringBuilder b = new StringBuilder();
        if (!TextUtils.isEmpty(args[0])) {
            b.append(args[0]);
        }
        for (int i = 1; i < args.length; i++) {
            CharSequence text = args[i];
            if (!TextUtils.isEmpty(text)) {
                if (b.length() > 0) {
                    b.append(mSeparator);
                }
                b.append(text);
            }
        }
        return b.toString();
    }

    CharSequence getCurrentMessage() {
        return mShowingMessage ? mMessage : null;
    }

    String getOwnerInfo() {
        ContentResolver res = getContext().getContentResolver();
        String info = null;
        final boolean ownerInfoEnabled = mLockPatternUtils.isOwnerInfoEnabled();
        if (ownerInfoEnabled && !mShowingMessage) {
            info = mLockPatternUtils.getOwnerInfo(mLockPatternUtils.getCurrentUser());
        }
        return info;
    }

    private CharSequence getChargeInfo(MutableInt icon, int idx) {
        CharSequence string = null;
        if (mShowingBatteryInfo[idx] && !mShowingMessage) {
            // Battery status
            if (mCharging[idx]) {
                // Charging, charged or waiting to charge.
                string = getContext().getString(mBatteryCharged[idx]
                        ? R.string.keyguard_charged
                        : R.string.keyguard_plugged_in, mBatteryLevel[idx]);
                icon.value = CHARGING_ICON;
            } else if (mBatteryIsLow[idx]) {
                // Battery is low
                string = getContext().getString(R.string.keyguard_low_battery);
                icon.value = BATTERY_LOW_ICON;
            } else { // only show battery level
                string = getContext().getString(R.string.lockscreen_battery_short, mBatteryLevel[idx]);
                icon.value = CHARGING_ICON;
            }

        }
        return string;
    }

    private void hideMessage(int duration, boolean thenUpdate) {
        if (duration > 0) {
            Animator anim = ObjectAnimator.ofFloat(this, "alpha", 0f);
            anim.setDuration(duration);
            if (thenUpdate) {
                anim.addListener(new AnimatorListenerAdapter() {
                        @Override
                            public void onAnimationEnd(Animator animation) {
                            update();
                        }
                });
            }
            anim.start();
        } else {
            setAlpha(0f);
            if (thenUpdate) {
                update();
            }
        }
    }

    private void showMessage(int duration) {
        if (duration > 0) {
            Animator anim = ObjectAnimator.ofFloat(this, "alpha", 1f);
            anim.setDuration(duration);
            anim.start();
        } else {
            setAlpha(1f);
        }
    }

    /**
     * Runnable used to delay accessibility announcements.
     */
    private static class AnnounceRunnable implements Runnable {
        private final WeakReference<View> mHost;
        private final CharSequence mTextToAnnounce;

        public AnnounceRunnable(View host, CharSequence textToAnnounce) {
            mHost = new WeakReference<View>(host);
            mTextToAnnounce = textToAnnounce;
        }

        @Override
        public void run() {
            final View host = mHost.get();
            if (host != null) {
                host.announceForAccessibility(mTextToAnnounce);
            }
        }
    }
    
    /// M: Mediatek add begin @{

    /// M: If dm lock is on, we should always show dm lock text
    private void setTextMediatek(CharSequence text) {
        StringBuilder b = new StringBuilder();
        if (text != null && text.length() > 0) {
            b.append(text);
            b.append(mSeparator);
        }
        b.append(getContext().getText(AntiTheftManager.getPrompt()));
        setText(b.toString());
    }

    /// M: [ALPS01413880] abnormal screen because of using HW layer
    @Override
    public void onAttachedToWindow() {
        super.onAttachedToWindow();
        buildLayer();
    }
    
    /// M: For memory leak issue
    @Override
    protected void onDetachedFromWindow() {
        super.onDetachedFromWindow();
        mHandler.removeCallbacks(mClearMessageRunnable);
        mUpdateMonitor.removeCallback(mInfoCallback);
        mUpdateMonitor = null;
    }
}
