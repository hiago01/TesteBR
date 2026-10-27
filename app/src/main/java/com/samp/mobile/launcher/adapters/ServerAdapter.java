package com.samp.mobile.launcher.adapters;

import android.app.Activity;
import android.content.Context;
import android.os.AsyncTask;
import android.transition.Fade;
import android.util.Log;
import android.util.Pair;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.constraintlayout.widget.ConstraintLayout;
import androidx.recyclerview.widget.RecyclerView;

import com.joom.paranoid.Obfuscate;
import com.samp.mobile.R;
import com.samp.mobile.launcher.MainActivity;
import com.samp.mobile.launcher.util.ButtonAnimator;
import com.samp.mobile.launcher.util.SAMPServerInfo;
import com.samp.mobile.launcher.util.SampQueryAPI;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
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
        View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.server_item, parent, false);

        return new ViewHolder(view);
    }

    @Override
    public void onBindViewHolder(ViewHolder holder, int position) {

        this.position = position;
        final ArrayList<SAMPServerInfo> list = ((MainActivity)mContext).getServerList();
        if (position < 0 || position >= list.size()) return;
        final SAMPServerInfo info = list.get(position);

        // Linha de estado (sem servidor real): carregando ou erro recuperavel.
        final boolean placeholder = info.getServerStatus() == SAMPServerInfo.Status.ERROR
                || (info.getServerStatus() == SAMPServerInfo.Status.LOADING && info.getPort() == 0);
        if (placeholder) {
            holder.mPassword.setImageResource(R.drawable.ic_password_off);
            holder.mServerNameText.setText(info.getServerName());
            holder.mOnlineText.setText(info.getServerStatus() == SAMPServerInfo.Status.ERROR ? "Error" : "Loading...");
            holder.mServerIP.setText(info.getAddress());
            holder.mMode.setText("");
            holder.mLayout.setOnTouchListener(new ButtonAnimator(mContext, holder.mLayout));
            holder.mLayout.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View view) {
                    if (info.getServerStatus() == SAMPServerInfo.Status.ERROR) {
                        ((MainActivity) mContext).getServersInfo(); // tentar novamente
                    }
                }
            });
            return;
        }

        if (!info.getHasPassword())
            holder.mPassword.setImageResource(R.drawable.ic_password_off);
        else
            holder.mPassword.setImageResource(R.drawable.ic_password_on);

        holder.mServerNameText.setText(info.getServerName());

        holder.mOnlineText.setText(info.getStatusText());
        holder.mServerIP.setText("Adress: " + info.getAddress() + ":" + info.getPort());
        holder.mMode.setText("Mode: " + info.getServerMode());

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
        public ImageView mPassword;
        public TextView mServerIP;
        public TextView mServerNameText;
        public TextView mOnlineText;
        public TextView mMode;
        public ConstraintLayout mLayout;

        public ViewHolder(View view) {
            super(view);
            mView = view;
            mLayout = view.findViewById(R.id.server_item_layout);
            mServerIP = view.findViewById(R.id.textView16);
            mPassword = view.findViewById(R.id.password);
            mServerNameText = view.findViewById(R.id.textView6);
            mOnlineText = view.findViewById(R.id.online_text);
            mMode = view.findViewById(R.id.textView18);
        }

        public View getView() {
            return mView;
        }
    }
}
