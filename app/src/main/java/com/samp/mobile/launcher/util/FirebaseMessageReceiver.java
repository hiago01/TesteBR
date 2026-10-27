package com.samp.mobile.launcher.util;

import com.google.firebase.messaging.FirebaseMessagingService;
import com.google.firebase.messaging.RemoteMessage;

import java.util.Map;

/**
 * Recebe mensagens do Firebase e delega a exibicao ao NotificationHelper.
 *
 * Formato recomendado (mensagem somente de DADOS, para funcionar tambem com o app fechado):
 *   { "title": "...", "message": "...", "image": "https://...", "type": "news|update|event", "url": "https://..." }
 * "image" e "url" sao opcionais; apenas http/https sao aceitos.
 */
public class FirebaseMessageReceiver extends FirebaseMessagingService {

    @Override
    public void onMessageReceived(RemoteMessage remoteMessage) {
        Map<String, String> data = remoteMessage.getData();
        if (data != null && !data.isEmpty()) {
            String message = data.get("message");
            if (message == null) message = data.get("body");
            NotificationHelper.showRemote(this, data.get("title"), message,
                    data.get("image"), data.get("type"), data.get("url"));
            return;
        }

        RemoteMessage.Notification n = remoteMessage.getNotification();
        if (n != null) {
            String image = n.getImageUrl() != null ? n.getImageUrl().toString() : null;
            NotificationHelper.showRemote(this, n.getTitle(), n.getBody(), image, "news", null);
        }
    }
}
