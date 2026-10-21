package com.bindersniffer.testapp;

import android.app.Activity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.os.Bundle;

public class MainActivity extends Activity {

    private static final String CHANNEL_ID = "bs_channel";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        NotificationManager nm = getSystemService(NotificationManager.class);

        // Required on API 26+: register a channel before posting.
        // -> INotificationManager.createNotificationChannels (binder call)
        NotificationChannel channel = new NotificationChannel(
                CHANNEL_ID, "Binder Sniffer", NotificationManager.IMPORTANCE_DEFAULT);
        nm.createNotificationChannel(channel);

        Notification notification = new Notification.Builder(this, CHANNEL_ID)
                .setContentTitle("Binder Sniffer")
                .setContentText("hello from testapp")
                .setSmallIcon(android.R.drawable.ic_dialog_info)
                .build();

        // -> INotificationManager.enqueueNotificationWithTag (binder call)
        nm.notify(1, notification);
    }
}
