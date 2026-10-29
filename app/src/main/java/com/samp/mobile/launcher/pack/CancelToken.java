package com.samp.mobile.launcher.pack;

import java.util.concurrent.atomic.AtomicBoolean;

/** Cancelamento cooperativo (onDestroy do servico / nova sessao). */
public final class CancelToken {
    private final AtomicBoolean cancelled = new AtomicBoolean(false);

    public void cancel() { cancelled.set(true); }
    public boolean isCancelled() { return cancelled.get(); }

    /** Lanca {@link Cancelled} se cancelado. */
    public void check() throws Cancelled {
        if (cancelled.get()) throw new Cancelled();
    }

    public static final class Cancelled extends PackException {
        public Cancelled() { super("cancelled"); }
    }
}
