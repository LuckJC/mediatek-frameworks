/*
 * Copyright (C) 2008 The Android Open Source Project
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

package com.android.server;

import android.app.IActivityController;
import android.os.Binder;
import android.os.RemoteException;
import com.android.server.am.ActivityManagerService;
import com.android.server.power.PowerManagerService;

import android.app.AlarmManager;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.BatteryManager;
import android.os.Debug;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.os.ServiceManager;
import android.os.SystemClock;
import android.os.SystemProperties;
import android.util.EventLog;
import android.util.Log;
import android.util.Slog;

import java.io.File;
import java.io.FileWriter;
import java.io.FileDescriptor;
import java.io.IOException;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.util.ArrayList;
import java.util.Calendar;

import com.mediatek.common.aee.IExceptionLog;
import com.mediatek.common.MediatekClassFactory;
/** This class calls its monitor every minute. Killing this process if they don't return **/
public class Watchdog extends Thread {
    static final String TAG = "Watchdog";
    static final boolean localLOGV = false || false;

    // Set this to true to use debug default values.
    static final boolean DB = false;

    // Set this to true to have the watchdog record kernel thread stacks when it fires
    static final boolean RECORD_KERNEL_THREADS = true;

    static final long DEFAULT_TIMEOUT = DB ? 10*1000 : 60*1000;
    static final long CHECK_INTERVAL = DEFAULT_TIMEOUT / 2;

    // These are temporally ordered: larger values as lateness increases
    static final int COMPLETED = 0;
    static final int WAITING = 1;
    static final int WAITED_HALF = 2;
    static final int OVERDUE = 3;
	static final int TIME_SF_WAIT = 20000;

    // Which native processes to dump into dropbox's stack traces
    public static final String[] NATIVE_STACKS_OF_INTEREST = new String[] {
        "/system/bin/mediaserver",
        "/system/bin/sdcard",
        "/system/bin/surfaceflinger"
    };

    static Watchdog sWatchdog;
	IExceptionLog exceptionHWT;

    /* This handler will be used to post message back onto the main thread */
    final ArrayList<HandlerChecker> mHandlerCheckers = new ArrayList<HandlerChecker>();
    final HandlerChecker mMonitorChecker;
    ContentResolver mResolver;
    BatteryService mBattery;
    PowerManagerService mPower;
    AlarmManagerService mAlarm;
    ActivityManagerService mActivity;

    int mPhonePid;
    IActivityController mController;
    boolean mAllowRestart = true;
	public static int GetSFStatus() {
		return SystemProperties.getInt("service.sf.status", 0);
	}	

	public static int GetSFReboot() {
		return SystemProperties.getInt("service.sf.reboot", 0);
	}
	
	public static void SetSFReboot(){
		int OldTime = SystemProperties.getInt("service.sf.reboot", 0);
		OldTime = OldTime + 1;
		if(OldTime > 9) OldTime = 9;
		SystemProperties.set("service.sf.reboot",String.valueOf(OldTime));
	}


    /**
     * Used for checking status of handle threads and scheduling monitor callbacks.
     */
    public final class HandlerChecker implements Runnable {
        private final Handler mHandler;
        private final String mName;
        private final long mWaitMax;
        private final ArrayList<Monitor> mMonitors = new ArrayList<Monitor>();
        private boolean mCompleted;
        private Monitor mCurrentMonitor;
        private long mStartTime;

        HandlerChecker(Handler handler, String name, long waitMaxMillis) {
            mHandler = handler;
            mName = name;
            mWaitMax = waitMaxMillis;
            mCompleted = true;
        }

        public void addMonitor(Monitor monitor) {
            mMonitors.add(monitor);
        }

        public void scheduleCheckLocked() {
            if (mMonitors.size() == 0 && mHandler.getLooper().isIdling()) {
                // If the target looper is or just recently was idling, then
                // there is no reason to enqueue our checker on it since that
                // is as good as it not being deadlocked.  This avoid having
                // to do a context switch to check the thread.  Note that we
                // only do this if mCheckReboot is false and we have no
                // monitors, since those would need to be executed at this point.
                mCompleted = true;
                return;
            }

            if (!mCompleted) {
                // we already have a check in flight, so no need
                return;
            }

            mCompleted = false;
            mCurrentMonitor = null;
            mStartTime = SystemClock.uptimeMillis();
            mHandler.postAtFrontOfQueue(this);
        }

        public boolean isOverdueLocked() {
            return (!mCompleted) && (SystemClock.uptimeMillis() > mStartTime + mWaitMax);
        }

        public int getCompletionStateLocked() {
            if (mCompleted) {
                return COMPLETED;
            } else {
                long latency = SystemClock.uptimeMillis() - mStartTime;
                if (latency < mWaitMax/2) {
                    return WAITING;
                } else if (latency < mWaitMax) {
                    return WAITED_HALF;
                }
            }
            return OVERDUE;
        }

        public Thread getThread() {
            return mHandler.getLooper().getThread();
        }

        public String getName() {
            return mName;
        }

        public String describeBlockedStateLocked() {
            if (mCurrentMonitor == null) {
                return "Blocked in handler on " + mName + " (" + getThread().getName() + ")";
            } else {
                return "Blocked in monitor " + mCurrentMonitor.getClass().getName()
                        + " on " + mName + " (" + getThread().getName() + ")";
            }
        }

        @Override
        public void run() {
            final int size = mMonitors.size();
            for (int i = 0 ; i < size ; i++) {
                synchronized (Watchdog.this) {
                    mCurrentMonitor = mMonitors.get(i);
                }
                mCurrentMonitor.monitor();
            }

            synchronized (Watchdog.this) {
                mCompleted = true;
                mCurrentMonitor = null;
            }
        }
    }

    final class RebootRequestReceiver extends BroadcastReceiver {
        @Override
        public void onReceive(Context c, Intent intent) {
            if (intent.getIntExtra("nowait", 0) != 0) {
                rebootSystem("Received ACTION_REBOOT broadcast");
                return;
            }
            Slog.w(TAG, "Unsupported ACTION_REBOOT broadcast: " + intent);
        }
    }

    public interface Monitor {
        void monitor();
    }

    public static Watchdog getInstance() {
        if (sWatchdog == null) {
            sWatchdog = new Watchdog();
        }

        return sWatchdog;
    }

    private Watchdog() {
        super("watchdog");
        // Initialize handler checkers for each common thread we want to check.  Note
        // that we are not currently checking the background thread, since it can
        // potentially hold longer running operations with no guarantees about the timeliness
        // of operations there.

        // The shared foreground thread is the main checker.  It is where we
        // will also dispatch monitor checks and do other work.
        mMonitorChecker = new HandlerChecker(FgThread.getHandler(),
                "foreground thread", DEFAULT_TIMEOUT);
        mHandlerCheckers.add(mMonitorChecker);
        // Add checker for main thread.  We only do a quick check since there
        // can be UI running on the thread.
        mHandlerCheckers.add(new HandlerChecker(new Handler(Looper.getMainLooper()),
                "main thread", DEFAULT_TIMEOUT));
        // Add checker for shared UI thread.
        mHandlerCheckers.add(new HandlerChecker(UiThread.getHandler(),
                "ui thread", DEFAULT_TIMEOUT));
        // And also check IO thread.
        mHandlerCheckers.add(new HandlerChecker(IoThread.getHandler(),
                "i/o thread", DEFAULT_TIMEOUT));
		try {
            exceptionHWT = MediatekClassFactory.createInstance(IExceptionLog.class);
        }
        catch (Exception e) {
            // AEE disabled or failed to allocate AEE object, no need to show message
        }
    }

    public void init(Context context, BatteryService battery,
            PowerManagerService power, AlarmManagerService alarm,
            ActivityManagerService activity) {
        mResolver = context.getContentResolver();
        mBattery = battery;
        mPower = power;
        mAlarm = alarm;
        mActivity = activity;

        context.registerReceiver(new RebootRequestReceiver(),
                new IntentFilter(Intent.ACTION_REBOOT),
                android.Manifest.permission.REBOOT, null);
    }

    public void processStarted(String name, int pid) {
        synchronized (this) {
            if ("com.android.phone".equals(name)) {
                mPhonePid = pid;
            }
        }
    }

    public void setActivityController(IActivityController controller) {
        synchronized (this) {
            mController = controller;
        }
    }

    public void setAllowRestart(boolean allowRestart) {
        synchronized (this) {
            mAllowRestart = allowRestart;
        }
    }

    public void addMonitor(Monitor monitor) {
        synchronized (this) {
            if (isAlive()) {
                throw new RuntimeException("Monitors can't be added once the Watchdog is running");
            }
            mMonitorChecker.addMonitor(monitor);
        }
    }

    public void addThread(Handler thread, String name) {
        addThread(thread, name, DEFAULT_TIMEOUT);
    }

    public void addThread(Handler thread, String name, long timeoutMillis) {
        synchronized (this) {
            if (isAlive()) {
                throw new RuntimeException("Threads can't be added once the Watchdog is running");
            }
            mHandlerCheckers.add(new HandlerChecker(thread, name, timeoutMillis));
        }
    }

    /**
     * Perform a full reboot of the system.
     */
    void rebootSystem(String reason) {
        Slog.i(TAG, "Rebooting system because: " + reason);
        PowerManagerService pms = (PowerManagerService) ServiceManager.getService("power");
        pms.reboot(false, reason, false);
    }

    private int evaluateCheckerCompletionLocked() {
        int state = COMPLETED;
        for (int i=0; i<mHandlerCheckers.size(); i++) {
            HandlerChecker hc = mHandlerCheckers.get(i);
            state = Math.max(state, hc.getCompletionStateLocked());
        }
        return state;
    }

    private ArrayList<HandlerChecker> getBlockedCheckersLocked() {
        ArrayList<HandlerChecker> checkers = new ArrayList<HandlerChecker>();
        for (int i=0; i<mHandlerCheckers.size(); i++) {
            HandlerChecker hc = mHandlerCheckers.get(i);
            if (hc.isOverdueLocked()) {
                checkers.add(hc);
            }
        }
        return checkers;
    }

    private String describeCheckersLocked(ArrayList<HandlerChecker> checkers) {
        StringBuilder builder = new StringBuilder(128);
        for (int i=0; i<checkers.size(); i++) {
            if (builder.length() > 0) {
                builder.append(", ");
            }
            builder.append(checkers.get(i).describeBlockedStateLocked());
        }
        return builder.toString();
    }
/*
	private void CputimeEnable(String bootevent){		  
     try {
            FileOutputStream fcputime = new FileOutputStream("/proc/mtprof/cputime");
			fcputime.write(bootevent.getBytes());
            fcputime.close();
            
        } catch (FileNotFoundException e) {
            Slog.e(TAG, "cputime entry can not found!", e);
        } catch (java.io.IOException e) {
            Slog.e(TAG, "cputime entry open fail", e);
        }

	}
*/
    @Override
    public void run() {
        boolean waitedHalf = false;
        boolean mNeedDump = false;
		boolean mSFHang = false;
        while (true) {
            final ArrayList<HandlerChecker> blockedCheckers;
            String subject;

			mSFHang = false;
			if(exceptionHWT!= null){					
				exceptionHWT.WDTMatterJava(300);
			}
            if(mNeedDump){
						// We've waited half the deadlock-detection interval.  Pull a stack
                // trace and wait another half.
                if(exceptionHWT!= null) exceptionHWT.WDTMatterJava(600);
                dumpAllBackTraces(true);
                mNeedDump = false;
            }
            
            final boolean allowRestart;
            synchronized (this) {
                long timeout = CHECK_INTERVAL;
				int SFHangTime;
                // Make sure we (re)spin the checkers that have become idle within
                // this wait-and-check interval
                for (int i=0; i<mHandlerCheckers.size(); i++) {
                    HandlerChecker hc = mHandlerCheckers.get(i);
                    hc.scheduleCheckLocked();
                }

                // NOTE: We use uptimeMillis() here because we do not want to increment the time we
                // wait while asleep. If the device is asleep then the thing that we are waiting
                // to timeout on is asleep as well and won't have a chance to run, causing a false
                // positive on when to kill things.
				//CputimeEnable(new String("1"));
                long start = SystemClock.uptimeMillis();
                while (timeout > 0) {
                    try {
                        wait(timeout);
                    } catch (InterruptedException e) {
                        Log.wtf(TAG, e);
                    }
                    timeout = CHECK_INTERVAL - (SystemClock.uptimeMillis() - start);
                }

				//MTK enhance
				SFHangTime = GetSFStatus();				
				if(SFHangTime > TIME_SF_WAIT * 2){
					Slog.v(TAG, "**SF hang Time **" + SFHangTime);
					mSFHang = true;
					
				} //@@
				else {
	                final int waitState = evaluateCheckerCompletionLocked();
	                if (waitState == COMPLETED) {
	                    // The monitors have returned; reset
	                    waitedHalf = false;
						//CputimeEnable(new String("0"));
	                    continue;
	                } else if (waitState == WAITING) {
	                    // still waiting but within their configured intervals; back off and recheck
	                   // CputimeEnable(new String("0"));
	                    continue;
	                } else if (waitState == WAITED_HALF) {
	                    if (!waitedHalf) {
	                        // We've waited half the deadlock-detection interval.  Pull a stack
	                        // trace and wait another half.
	                        mNeedDump = true;
							waitedHalf = true;
	                    }
	                    continue;
	                }
				}
                // something is overdue!
                blockedCheckers = getBlockedCheckersLocked();
                subject = describeCheckersLocked(blockedCheckers);
                allowRestart = mAllowRestart;
            }
			//CputimeEnable(new String("0"));

            // If we got here, that means that the system is most likely hung.
            // First collect stack traces from all threads of the system process.
            // Then kill this process so that the system will restart.
			Slog.e(TAG, "**SWT happen **" + subject);
			if(mSFHang && subject.isEmpty()){
				subject = "surfaceflinger  hang.";
			}
            EventLog.writeEvent(EventLogTags.WATCHDOG, subject);

            if(exceptionHWT!= null) exceptionHWT.WDTMatterJava(720);
            dumpAllBackTraces(false);
           

            // Give some extra time to make sure the stack traces get written.
            // The system's been hanging for a minute, another second or two won't hurt much.
            SystemClock.sleep(2000);

            // Pull our own kernel thread stacks as well if we're configured for that
            if (RECORD_KERNEL_THREADS) {
                dumpKernelStackTraces();
            }

            // Trigger the kernel to dump all blocked threads to the kernel log
            try {
                FileWriter sysrq_trigger = new FileWriter("/proc/sysrq-trigger");
                sysrq_trigger.write("w");
                sysrq_trigger.close();
            } catch (IOException e) {
                Slog.e(TAG, "Failed to write to /proc/sysrq-trigger");
                Slog.e(TAG, e.getMessage());
            }

            /// M: WDT debug enhancement
            /// need to wait the AEE dumps all info, then kill system server @{
            /*
            // Try to add the error to the dropbox, but assuming that the ActivityManager
            // itself may be deadlocked.  (which has happened, causing this statement to
            // deadlock and the watchdog as a whole to be ineffective)
            Thread dropboxThread = new Thread("watchdogWriteToDropbox") {
                    public void run() {
                        mActivity.addErrorToDropBox(
                                "watchdog", null, "system_server", null, null,
                                name, null, stack, null);
                    }
                };
            dropboxThread.start();
            try {
                dropboxThread.join(2000);  // wait up to 2 seconds for it to return.
            } catch (InterruptedException ignored) {}
            */
            Slog.v(TAG, "** save all info before killnig system server **");
            mActivity.addErrorToDropBox("watchdog", null, "system_server", null, null, subject, null, null, null);

            IActivityController controller;
            synchronized (this) {
                controller = mController;
            }
            if (controller != null) {
                Slog.i(TAG, "Reporting stuck state to activity controller");
                try {
                    Binder.setDumpDisabled("Service dumps disabled due to hung system process.");
					Slog.i(TAG, "Binder.setDumpDisabled");
                    // 1 = keep waiting, -1 = kill system
                    int res = controller.systemNotResponding(subject);
                    if (res >= 0) {
                        Slog.i(TAG, "Activity controller requested to coninue to wait");
                        waitedHalf = false;
                        continue;
                    }
					Slog.i(TAG, "Activity controller requested to reboot");
                } catch (RemoteException e) {
                }
            }

            // Only kill the process if the debugger is not attached.
            if (Debug.isDebuggerConnected()) {
                Slog.w(TAG, "Debugger connected: Watchdog is *not* killing the system process");
            } else if (!allowRestart) {
                Slog.w(TAG, "Restart not allowed: Watchdog is *not* killing the system process");
            } else {
                Slog.w(TAG, "*** WATCHDOG KILLING SYSTEM PROCESS: " + subject);
                for (int i=0; i<blockedCheckers.size(); i++) {
                    Slog.w(TAG, blockedCheckers.get(i).getName() + " stack trace:");
                    StackTraceElement[] stackTrace
                            = blockedCheckers.get(i).getThread().getStackTrace();
                    for (StackTraceElement element: stackTrace) {
                        Slog.w(TAG, "    at " + element);
                    }
                }
                SystemClock.sleep(25000);
                /// @}

                Slog.w(TAG, "*** GOODBYE!");
				// MTK enhance
				if(mSFHang)
				{
					Slog.w(TAG, "SF hang!");
					if(GetSFReboot() > 3)
					{
						Slog.w(TAG, "SF hang reboot time larger than 3 time, reboot device!");
						rebootSystem("Maybe SF driver hang,reboot device.");
					}
					else
					{
						SetSFReboot();
					}
				}
				//@
				
                Process.killProcess(Process.myPid());
                System.exit(10);
            }

            waitedHalf = false;
        }
    }

    private File dumpKernelStackTraces() {
        String tracesPath = SystemProperties.get("dalvik.vm.stack-trace-file", null);
        if (tracesPath == null || tracesPath.length() == 0) {
            return null;
        }

        native_dumpKernelStacks(tracesPath);
        return new File(tracesPath);
    }

    private native void native_dumpKernelStacks(String tracesPath);
    /**
     * M: WDT debug enhancement     
     */
    private File dumpAllBackTraces(boolean clearTraces) {
        /*debug flag for dump all thread backtrace for ANR*/

		ArrayList<Integer> pids = new ArrayList<Integer>();

	    /// M: WDT debug enhancement
	    /// it's better to dump all running processes backtraces
	    /// and integrate with AEE @{
	 
	    // pids.add(Process.myPid());
	    //if (mPhonePid > 0) pids.add(mPhonePid);
	    // Pass !waitedHalf so that just in case we somehow wind up here without having
          
        mActivity.getRunningProcessPids(pids);
        File stack = ActivityManagerService.dumpStackTraces(true, pids, null, null, NATIVE_STACKS_OF_INTEREST);
    
        return stack; 
    }
}
