package com.samp.mobile.launcher.adapters;

import android.app.Activity;
import android.content.Context;
import android.util.Log;
import android.util.Pair;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.constraintlayout.widget.ConstraintLayout;
import androidx.recyclerview.widget.RecyclerView;

import com.joom.paranoid.Obfuscate;
import com.samp.mobile.R;
import com.samp.mobile.launcher.MainActivity;
import com.samp.mobile.launcher.util.ButtonAnimator;
import com.samp.mobile.launcher.util.SAMPServerInfo;
import com.samp.mobile.launcher.util.ServerConnector;

import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;

/**
 * Lista da aba Servidores (agora com o cartao server_card). Continua lendo SEMPRE a lista
 * compartilhada de MainActivity (no maximo 1 item), entao nunca duplica registros.
 * Nenhum dado e inventado: jogadores/ping/modalidade so aparecem quando a consulta UDP os devolveu.
 */
@Obfuscate
public class ServerAdapter extends RecyclerView.Adapter<ServerAdapter.ViewHolder> {

    public Context mContext;
    public ArrayList<SAMPServerInfo> mServersInfo = new ArrayList<>();
    public ArrayList<SAMPServerInfo> mServersInfoFavourite = new ArrayList<>();
    public File mServerFavouriteFile;
    int position = 0;

    private int pagePosition;
    private final HashMap<Integer, Pair<String, Integer>> holderPosition = new HashMap<>();

    public ServerAdapter(Context context, int page)
    {
        mContext = context;
        pagePosition = page;
        refreshServers();
        Log.d("InfoJSON", "ServerAdapter " + mServersInfo.size());
    }

    public int getPagePosition()
    {
        return pagePosition;
    }

    @Override
    public ViewHolder onCreateViewHolder(ViewGroup parent, int viewType) {
        View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.server_card, parent, false);
        return new ViewHolder(view);
    }

    @Override
    public void onBindViewHolder(final ViewHolder holder, int position) {

        this.position = position;
        final ArrayList<SAMPServerInfo> list = ((MainActivity)mContext).getServerList();
        if (position < 0 || position >= list.size()) return;
        final SAMPServerInfo info = list.get(position);
        final SAMPServerInfo.Status st = info.getServerStatus();
        final String dash = mContext.getString(R.string.mx_unknown);

        // Sem servidor real: carregando a lista (porta 0) ou erro ao obter servers.json.
        final boolean noServer = st == SAMPServerInfo.Status.ERROR
                || (st == SAMPServerInfo.Status.LOADING && info.getPort() == 0);

        holder.mName.setText(info.getServerName());
        applyStatus(holder, st);

        if (noServer) {
            holder.mLock.setVisibility(View.GONE);
            holder.mMode.setText(dash);
            holder.mAddress.setText(dash);
            holder.mPort.setText(dash);
            holder.mPlayers.setText(dash);
            holder.mPing.setText(dash);
            boolean error = st == SAMPServerInfo.Status.ERROR;
            holder.mHint.setVisibility(error ? View.VISIBLE : View.GONE);
            holder.mConnect.setEnabled(false);
            holder.mConnect.setOnClickListener(null);
            holder.mLayout.setOnTouchListener(null);
            holder.mLayout.setOnClickListener(error ? new View.OnClickListener() {
                @Override
                public void onClick(View view) {
                    ((MainActivity) mContext).refreshAll(); // tentar novamente
                }
            } : null);
            return;
        }

        holder.mHint.setVisibility(View.GONE);
        holder.mLock.setVisibility(info.getHasPassword() ? View.VISIBLE : View.GONE);

        String mode = info.getServerMode();
        holder.mMode.setText(mode != null && !mode.trim().isEmpty() ? mode : dash);
        holder.mAddress.setText(info.getAddress());
        holder.mPort.setText(String.valueOf(info.getPort()));

        boolean online = st == SAMPServerInfo.Status.ONLINE;
        boolean hasCounts = online && info.getCurrentPlayerCount() >= 0 && info.getMaxPlayerCount() >= 0;
        holder.mPlayers.setText(hasCounts ? info.getCurrentPlayerCount() + "/" + info.getMaxPlayerCount() : dash);
        holder.mPing.setText(online && info.getPing() >= 0 ? info.getPing() + " ms" : dash);

        // Conectar: mesmo fluxo do botao Connect original (ServerConnector).
        holder.mConnect.setEnabled(true);
        holder.mConnect.setOnTouchListener(new ButtonAnimator(mContext, holder.mConnect));
        holder.mConnect.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                ServerConnector.connect((Activity) mContext, info);
            }
        });

        // Tocar no cartao abre os detalhes (dialogo original).
        holder.mLayout.setOnTouchListener(new ButtonAnimator(mContext, holder.mLayout));
        holder.mLayout.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                ServerInformationFragment newInstance = new ServerInformationFragment((Activity) mContext, ServerAdapter.this, holder.getAdapterPosition(), info);
                newInstance.show();
            }
        });

        holderPosition.put(Integer.valueOf(position), new Pair<>(info.getAddress(), Integer.valueOf(info.getPort())));
    }

    /** Chip de estado: ONLINE / OFFLINE / ERRO DE CONEXAO / VERIFICANDO..., conforme o resultado real. */
    private void applyStatus(ViewHolder h, SAMPServerInfo.Status st) {
        int text, bg, color;
        switch (st) {
            case ONLINE:
                text = R.string.mx_status_online;  bg = R.drawable.mx_chip_online;  color = R.color.mx_online;  break;
            case OFFLINE:
                text = R.string.mx_status_offline; bg = R.drawable.mx_chip_offline; color = R.color.mx_offline; break;
            case ERROR:
                text = R.string.mx_status_error;   bg = R.drawable.mx_chip_error;   color = R.color.mx_error;   break;
            default:
                text = R.string.mx_status_checking; bg = R.drawable.mx_chip_loading; color = R.color.mx_loading; break;
        }
        h.mStatus.setText(text);
        h.mStatus.setBackgroundResource(bg);
        h.mStatus.setTextColor(mContext.getResources().getColor(color));
    }

    public void refreshServers() {
        // mServersInfo pode apontar para a lista compartilhada: nunca limpar aqui.
        getLocalServers();
        notifyDataSetChanged();
    }

    public void getLocalServers() {
        mServersInfo = ((MainActivity)mContext).getServerList();
    }

    @Override
    public int getItemCount() {
        return ((MainActivity)mContext).getServerList().size();
    }

    public class ViewHolder extends RecyclerView.ViewHolder
    {
        private View mView;
        public ImageView mLock;
        public TextView mName;
        public TextView mStatus;
        public TextView mMode;
        public TextView mAddress;
        public TextView mPort;
        public TextView mPlayers;
        public TextView mPing;
        public TextView mHint;
        public Button mConnect;
        public ConstraintLayout mLayout;

        public ViewHolder(View view) {
            super(view);
            mView = view;
            mLayout = view.findViewById(R.id.server_item_layout);
            mLock = view.findViewById(R.id.card_lock);
            mName = view.findViewById(R.id.card_name);
            mStatus = view.findViewById(R.id.card_status);
            mMode = view.findViewById(R.id.card_mode);
            mAddress = view.findViewById(R.id.card_address);
            mPort = view.findViewById(R.id.card_port);
            mPlayers = view.findViewById(R.id.card_players);
            mPing = view.findViewById(R.id.card_ping);
            mHint = view.findViewById(R.id.card_hint);
            mConnect = view.findViewById(R.id.card_connect);
        }

        public View getView() {
            return mView;
        }
    }
}
