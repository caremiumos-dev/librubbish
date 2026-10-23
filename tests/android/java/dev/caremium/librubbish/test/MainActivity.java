package dev.caremium.librubbish.test;

import android.app.Activity;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.util.Log;

import com.xtc.secretapi.XTCGetUtilWrapper;

import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;

public final class MainActivity extends Activity {
    private static final String TAG = "librubbish_test";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        String outcome;
        try {
            XTCGetUtilWrapper wrapper = new XTCGetUtilWrapper(getApplicationContext());
            String expected = packageSignatureSha1Hex();
            String gete = wrapper.gete();
            String accepted = wrapper.init(expected);
            String rejected = wrapper.init("BAD");

            outcome = "gete=" + gete
                    + ";accepted=" + accepted
                    + ";rejected=" + rejected;

            if (!"xtc_ok".equals(gete)
                    || !"xtc_ok".equals(accepted)
                    || !"xtc_error".equals(rejected)) {
                throw new AssertionError("unexpected result: " + outcome);
            }
            Log.i(TAG, "PASS " + outcome);
        }
        catch (Throwable throwable) {
            outcome = "FAIL " + throwable;
            Log.e(TAG, outcome, throwable);
        }

        try (FileOutputStream stream = openFileOutput("result.txt", MODE_PRIVATE)) {
            stream.write(outcome.getBytes(StandardCharsets.UTF_8));
        }
        catch (Exception exception) {
            Log.e(TAG, "cannot write result", exception);
        }
        finish();
    }

    private String packageSignatureSha1Hex() throws Exception {
        PackageManager manager = getPackageManager();
        PackageInfo info = manager.getPackageInfo(
                getPackageName(),
                PackageManager.GET_SIGNATURES);
        byte[] certificate = info.signatures[0].toByteArray();
        byte[] digest = MessageDigest.getInstance("SHA-1").digest(certificate);

        char[] hex = "0123456789ABCDEF".toCharArray();
        char[] encoded = new char[digest.length * 2];
        for (int i = 0; i < digest.length; ++i) {
            int value = digest[i] & 0xFF;
            encoded[i * 2] = hex[value >>> 4];
            encoded[i * 2 + 1] = hex[value & 0x0F];
        }
        return new String(encoded);
    }
}
