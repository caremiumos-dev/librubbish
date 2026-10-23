package com.xtc.secretapi;

import android.content.Context;
import android.content.ContextWrapper;

public final class XTCGetUtilWrapper extends ContextWrapper {
    static {
        System.loadLibrary("rubbish");
    }

    public XTCGetUtilWrapper(Context base) {
        super(base);
    }

    public native String gete();

    public native String init(String expectedSignatureHash);
}
