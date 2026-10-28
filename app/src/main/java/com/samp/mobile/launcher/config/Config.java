package com.samp.mobile.launcher.config;

import android.app.Activity;

public class Config {

    public static Activity currentContext;

    /** true quando a verificacao do Splash concluiu que cliente/dados estao atualizados. */
    public static boolean clientUpToDate = false;

    /**
     * Categoria ANTIGA de favoritos (servidores adicionados manualmente por IP/porta). Desativada na
     * interface: a aba, o botao "Adicionar servidor" e as consultas em segundo plano ficam ocultos.
     * Toda a implementacao (FavoritesInfo, FavoriteServerData, adapters, dialogos e o arquivo
     * SAMP/favorites.json) permanece intacta; mude para true para reativar a aba.
     */
    public static final boolean SHOW_LEGACY_FAVORITES = false;

    /**
     * Favoritos dos servidores OFICIAIS (aba "Favoritos" + estrela). Desativado na interface: ficam
     * somente os servidores oficiais. OfficialFavorites e a pagina continuam no projeto; mude para true
     * para reativar a aba e a estrela.
     */
    public static final boolean SHOW_OFFICIAL_FAVORITES = false;
}
