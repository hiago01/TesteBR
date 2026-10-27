package com.samp.mobile.launcher.adapters;

import android.app.Activity;
import android.content.Context;
import android.util.Log;
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
@Obfuscate
public class FavouriteServerAdapter extends RecyclerView.Adapter<FavouriteServerAdapter.ViewHolder>{

    public Context mActivity;
    int position;


    public FavouriteServerAdapter(Activity activity)
    {
        mActivity = activity;
    }

    @Override
    public ViewHolder onCreateViewHolder(ViewGroup parent, int viewType) {
        View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.server_item, parent, false);

        return new ViewHolder(view);
    }

    @Override
    public void onBindViewHolder(final ViewHolder holder, int position) {
        final MainActivity main = (MainActivity) mActivity;
        final int pos = holder.getAdapterPosition();
        if (pos < 0 || pos >= main.getFavoriteServerList().size()) return;
        final SAMPServerInfo info = main.getFavoriteServerList().get(pos);

        ServerCardBinder.bind(holder.card, info, main.isSelected(info), true);

        holder.mLayout.setOnTouchListener(new ButtonAnimator(mActivity, holder.mLayout));
        holder.mLayout.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                main.selectServer(info);
            }
        });
        holder.card.chevron.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                int p = holder.getAdapterPosition();
                if (p < 0 || p >= main.getFavoriteServerList().size()) return;
                new FavouriteServerInformationFragment((Activity) mActivity, FavouriteServerAdapter.this, p, main.getFavoriteServerList().get(p)).show();
            }
        });
    }

    @Override
    public int getItemCount() {
        return ((MainActivity) mActivity).getFavoriteServerList().size();
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
        final ServerCardBinder.Views card;

        public ViewHolder(View view) {
            super(view);
            mView = view;
            mLayout = view.findViewById(R.id.server_item_layout);
            mServerIP = view.findViewById(R.id.textView16);
            mPassword = view.findViewById(R.id.password);
            mServerNameText = view.findViewById(R.id.textView6);
            mOnlineText = view.findViewById(R.id.online_text);
            mMode = view.findViewById(R.id.textView18);
            card = new ServerCardBinder.Views(view);
        }

        public View getView() {
            return mView;
        }
    }
}
