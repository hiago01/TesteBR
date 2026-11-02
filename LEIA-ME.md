# Android 15: correcoes da auditoria

Copie `app/` por cima do projeto (mesmos caminhos). Originais em `backup_originais/`.

1. build.gradle: ShadowHook 1.0.9 -> 1.0.10 (a 1.0.10 declara compatibilidade com Android 15 e paginas de 16 KB).
   CONFIRA que 1.0.10 resolve no seu Gradle (nao consegui consultar o Maven daqui).
2. AndroidManifest.xml: removido android:gwpAsanMode="always".
3. Download em servico em primeiro plano (dataSync):
   - Manifest: permissao FOREGROUND_SERVICE_DATA_SYNC + foregroundServiceType="dataSync" no UpdateService.
   - UpdateService: mDownloadingStatus passa a ser alterado por setDownloading(); ao comecar a baixar o servico vira
     foreground (notificacao de progresso), ao terminar sai com atraso de 3 s (as retentativas automaticas alternam false/true).
     Falha ao entrar em primeiro plano e apenas logada; o download segue como antes.
   - NotificationHelper: buildDownloadForeground / cancelDownloadIfOngoing / downloadNotificationId.
4. CMakeLists.txt: -Wl,-z,max-page-size=16384 nas libs compiladas aqui (libsamp etc.).

## NAO feito
- libbass.so, libbass_ssl.so, libbassmix.so (jniLibs/arm64-v8a) continuam alinhadas em 4 KB (binarios prontos, sem fonte).
  Precisam ser trocadas por builds 16 KB da BASS. Isso so afeta aparelhos Android 15 com paginas de 16 KB.
- O zip do APK so e alinhado em 16 KB com AGP >= 8.5.1 (hoje 8.2.1); nao atualizei o AGP para nao quebrar o build.

## NAO verificado
Compilacao (Gradle/NDK), comportamento em aparelho, e se a ShadowHook 1.0.10 compila/roda com o seu codigo nativo.
