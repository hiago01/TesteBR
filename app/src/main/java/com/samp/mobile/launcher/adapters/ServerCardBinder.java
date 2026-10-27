package com.samp.mobile.launcher.adapters;

import android.content.res.ColorStateList;
import android.view.View;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.core.widget.ImageViewCompat;

import com.samp.mobile.R;
import com.samp.mobile.launcher.MainActivity;
import com.samp.mobile.launcher.util.SAMPServerInfo;

/** Preenche o cartao server_item.xml com dados REAIS (nada de valores ficticios). */
final class ServerCardBinder {
    private ServerCardBinder() {}

    static final class Views {
        final View root;
        final TextView name, mode, address, chip, players, ping, password;
        final ImageView signal, lock, chevron;

        Views(View v) {
            root = v.findViewById(R.id.server_item_layout);
            name = v.findViewById(R.id.textView6);
            mode = v.findViewById(R.id.textView18);
            address = v.findViewById(R.id.textView16);
            chip = v.findViewById(R.id.mx_status_chip);
            players = v.findViewById(R.id.online_text);
            ping = v.findViewById(R.id.mx_ping_text);
            password = v.findViewById(R.id.mx_password_text);
            signal = v.findViewById(R.id.mx_signal_icon);
            lock = v.findViewById(R.id.password);
            chevron = v.findViewById(R.id.mx_chevron);
        }
    }

    static void bind(Views v, SAMPServerInfo info, boolean selected, boolean unqueriedIsUnknown) {
        v.root.setSelected(selected);
        SAMPServerInfo.Status st = info.getServerStatus();
        boolean placeholder = MainActivity.isPlaceholder(info);
        v.name.setText(info.getServerName());

        int color, chipBg, chipText;
        switch (st) {
            case ONLINE:  color = R.color.mx_online;   chipBg = R.drawable.mx_chip_online;   chipText = R.string.mx_status_online; break;
            case OFFLINE: color = R.color.mx_offline;  chipBg = R.drawable.mx_chip_offline;  chipText = R.string.mx_status_offline; break;
            case ERROR:   color = R.color.mx_warning;  chipBg = R.drawable.mx_chip_warning;  chipText = R.string.mx_status_error; break;
            default:      color = R.color.mx_checking; chipBg = R.drawable.mx_chip_checking; chipText = R.string.mx_status_checking; break;
        }
        int c = v.root.getResources().getColor(color, null);
        v.chip.setBackgroundResource(chipBg);
        v.chip.setText(chipText);
        v.chip.setTextColor(c);
        ImageViewCompat.setImageTintList(v.signal, ColorStateList.valueOf(c));
        v.ping.setTextColor(c);

        if (placeholder) {
            v.mode.setText("");
            v.address.setText(st == SAMPServerInfo.Status.ERROR ? "Toque para tentar novamente" : info.getAddress());
            v.players.setText("--");
            v.ping.setText(st == SAMPServerInfo.Status.ERROR ? "Erro" : "...");
            v.password.setText("--");
            v.chevron.setVisibility(View.INVISIBLE);
            return;
        }
        v.chevron.setVisibility(View.VISIBLE);

        String mode = info.getServerMode();
        String lang = info.getLanguage();
        StringBuilder sb = new StringBuilder();
        if (st == SAMPServerInfo.Status.ONLINE && mode != null && !mode.isEmpty()) sb.append(mode);
        if (st == SAMPServerInfo.Status.ONLINE && lang != null && !lang.isEmpty()) {
            if (sb.length() > 0) sb.append(" \u2022 ");
            sb.append(lang);
        }
        v.mode.setText(sb);
        v.address.setText(info.getAddress() + ":" + info.getPort());

        if (st == SAMPServerInfo.Status.ONLINE && info.getCurrentPlayerCount() >= 0 && info.getMaxPlayerCount() >= 0) {
            v.players.setText(info.getCurrentPlayerCount() + "/" + info.getMaxPlayerCount());
        } else {
            v.players.setText("--");
        }
        switch (st) {
            case ONLINE:
                v.ping.setText(info.getPing() > 0 ? "Online \u00b7 " + info.getPing() + " ms" : "Online");
                break;
            case OFFLINE: v.ping.setText("Offline"); break;
            default: v.ping.setText("Verificando"); break;
        }
        boolean unknown = unqueriedIsUnknown && st != SAMPServerInfo.Status.ONLINE;
        v.password.setText(unknown ? "--" : (info.getHasPassword() ? "Sim" : "N\u00e3o"));
    }
}
