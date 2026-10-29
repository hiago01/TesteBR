package com.samp.mobile.launcher.data;

public class FavoriteServerData {

    public int id = 0;

    public String ip = "201.33.84.96";
    public int port = 7780;
    public boolean queried;
    public int serverid = 0;

    public FavoriteServerData(int i, int i2, String str, int i3) {
        this.id = i;
        this.serverid = i2;
        this.ip = str;
        this.port = i3;
    }
}
