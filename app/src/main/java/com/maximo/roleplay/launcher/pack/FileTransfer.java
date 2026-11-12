package com.maximo.roleplay.launcher.pack;

import java.io.File;

/**
 * Motor de transferencia de UM arquivo grande (pacote .zip). A verificacao (tamanho + SHA-256), as tentativas e o
 * progresso total ficam em {@link PackDownloader}; aqui so se baixa.
 * {@link HttpTransfer} e a implementacao (JVM puro, HttpURLConnection) usada na aba Reparar e nos testes.
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
