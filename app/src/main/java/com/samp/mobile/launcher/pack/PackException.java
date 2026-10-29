package com.samp.mobile.launcher.pack;

/** Falha controlada do instalador de pacotes (mensagem ja pronta para o usuario/log). */
public class PackException extends Exception {
    public PackException(String message) { super(message); }
    public PackException(String message, Throwable cause) { super(message, cause); }
}
