package com.samp.mobile.launcher.pack;

import java.io.File;

/**
 * Motor de transferencia de UM arquivo grande (pacote .zip). A verificacao (tamanho + SHA-256), as tentativas e o
 * progresso total ficam em {@link PackDownloader}; aqui so se baixa.
 * No app a implementacao e o PRDownloader (com.samp.mobile.launcher.PrDownloaderTransfer, retoma de onde parou);
 * {@link HttpTransfer} e a implementacao em JVM puro usada nos testes.
 */
public interface FileTransfer {
    /** Bytes ja gravados deste arquivo (cumulativo, inclusive o que foi retomado). Pode diminuir se o download recomecar do zero. */
    interface Progress { void at(long bytesOfThisFile); }

    /**
     * Garante que dir/fileName exista COMPLETO (nao confere hash). Pode retomar um parcial deixado por uma chamada
     * anterior. Se lancar, o arquivo final nao existe; o parcial pode ficar para a proxima tentativa.
     * Deve lancar {@link CancelToken.Cancelled} quando cancel for acionado.
     */
    void fetch(String url, File dir, String fileName, long expectedSize, Progress progress, CancelToken cancel) throws PackException;

    /** Apaga o arquivo e qualquer parcial dele (usado quando o conteudo e invalido e nao deve ser retomado). */
    void discard(File dir, String fileName);
}
