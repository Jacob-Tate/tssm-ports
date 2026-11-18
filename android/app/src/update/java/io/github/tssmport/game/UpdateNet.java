package io.github.tssmport.game;

import java.io.IOException;
import java.io.InputStream;
import java.net.ConnectException;
import java.net.HttpURLConnection;
import java.net.NoRouteToHostException;
import java.net.PortUnreachableException;
import java.net.ProtocolException;
import java.net.SocketTimeoutException;
import java.net.URL;
import java.net.URLConnection;
import java.net.UnknownHostException;
import java.security.cert.CertPathValidatorException;
import java.security.cert.CertificateException;
import java.security.cert.CertificateExpiredException;
import java.security.cert.CertificateNotYetValidException;
import java.util.concurrent.atomic.AtomicBoolean;

import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLException;
import javax.net.ssl.SSLPeerUnverifiedException;

/**
 * One HTTPS GET for the in-app updater (src/SB/Core/pc/iUpdate.h,
 * iUpdatePlatform::Get), with the platform's own HttpsURLConnection.
 *
 * <p>Its checks stay as Android sets them: the chain to one of the system's
 * trusted roots, the host name and the certificate's dates. Nothing here
 * replaces the socket factory, the host name verifier or the trust store.
 * No redirect is followed: a 3xx goes to the sink like any other status, and
 * the core decides where to go next.
 *
 * <p>The request carries Host, User-Agent (the updater's own, nothing about
 * the device), Accept, Connection: close and Accept-Encoding: identity. The
 * last is there because Android's HTTP client otherwise asks for gzip and
 * unpacks it itself, which would hide the length the core checks.
 *
 * <p>No Android classes, so that a desktop JVM runs its test
 * (src/updateTest/java/.../UpdateNetTest.java).
 */
final class UpdateNet {

    // iUpdateNet in iUpdate.h, in its order.
    static final int OK = 0;
    static final int OFFLINE = 1;
    static final int DNS = 2;
    static final int CONNECT = 3;
    static final int TLS = 4;
    static final int CERT = 5;
    static final int CLOCK = 6;
    static final int TIMEOUT = 7;
    static final int LOST = 8;
    static final int PROTOCOL = 9;
    static final int CANCELLED = 10;
    static final int STOPPED = 11;
    static final int ERROR = 12;

    /** kUpdateUserAgent in iUpdate.h. */
    static final String USER_AGENT = "TSSM-port-updater";

    /** How often a cancel is looked for while the connection waits. */
    static final int CANCEL_POLL_MS = 100;

    private static final int CHUNK = 64 * 1024;

    /** What the core's iUpdateSink takes. */
    interface Sink {
        /** False: the body is not wanted. */
        boolean onResponse(int status, long contentLength, String location);

        /** False: stop (STOPPED). */
        boolean onBody(byte[] data, int n);

        /** True: stop (CANCELLED). Asked from another thread too. */
        boolean cancelled();
    }

    static final class Result {
        final int code;
        final String detail;

        Result(int code, String detail) {
            this.code = code;
            this.detail = detail != null ? detail : "";
        }
    }

    private UpdateNet() {
    }

    /**
     * GET https://host[:port]path. The port is 443 except in the test. Returns
     * OK once one whole response was read (whatever its status).
     */
    static Result get(String host, int port, String path, String accept, int timeoutMs,
            final Sink sink) {
        if (sink.cancelled()) {
            return new Result(CANCELLED, "");
        }
        final HttpsURLConnection c;
        try {
            // 443 left implicit, so the Host header is the bare name.
            @SuppressWarnings("deprecation")
            URL url = port == 443 ? new URL("https", host, path) : new URL("https", host, port, path);
            if (!host.equals(url.getHost()) || !path.equals(url.getFile())) {
                return new Result(ERROR, "the address did not stay as given");
            }
            URLConnection opened = url.openConnection();
            if (!(opened instanceof HttpsURLConnection)) {
                return new Result(ERROR, "no HTTPS connection for " + host);
            }
            c = (HttpsURLConnection) opened;
            c.setInstanceFollowRedirects(false);
            // No response cache is installed in the app; useCaches is left at
            // its default because turning it off makes some HTTP clients add
            // Cache-Control and Pragma to the request.
            c.setDoInput(true);
            c.setConnectTimeout(timeoutMs);
            c.setReadTimeout(timeoutMs);
            c.setRequestProperty("User-Agent", USER_AGENT);
            c.setRequestProperty("Accept", accept);
            c.setRequestProperty("Accept-Encoding", "identity");
            c.setRequestProperty("Connection", "close");
        } catch (IOException | RuntimeException e) {
            return new Result(ERROR, text(e));
        }

        // A cancel disconnects, which on Android closes the socket under
        // whatever connect or read the request is blocked in. Again on every
        // poll until the request ends: a disconnect before the connection
        // exists does nothing. The read loop also looks between reads.
        final AtomicBoolean done = new AtomicBoolean();
        final AtomicBoolean cancelled = new AtomicBoolean();
        Thread watcher = new Thread(new Runnable() {
            @Override
            public void run() {
                while (!done.get()) {
                    if (cancelled.get() || sink.cancelled()) {
                        cancelled.set(true);
                        c.disconnect();
                    }
                    try {
                        Thread.sleep(CANCEL_POLL_MS);
                    } catch (InterruptedException e) {
                        return;
                    }
                }
            }
        }, "tssm-update-cancel");
        watcher.setDaemon(true);
        watcher.start();

        boolean reading = false;
        try {
            int status = c.getResponseCode();
            if (status < 100 || status > 599) {
                return new Result(cancelled.get() ? CANCELLED : PROTOCOL,
                        "not an HTTP response (status " + status + ")");
            }
            long length = contentLength(c);
            String location = c.getHeaderField("Location");
            if (!sink.onResponse(status, length, location)) {
                return new Result(OK, "");
            }
            InputStream in = status >= 400 ? c.getErrorStream() : c.getInputStream();
            if (in == null) {
                return new Result(OK, "");
            }
            reading = true;
            long total = 0;
            byte[] buffer = new byte[CHUNK];
            try {
                while (true) {
                    if (cancelled.get() || sink.cancelled()) {
                        return new Result(CANCELLED, "");
                    }
                    int n = in.read(buffer);
                    if (n < 0) {
                        break;
                    }
                    if (n == 0) {
                        continue;
                    }
                    total += n;
                    if (!sink.onBody(buffer, n)) {
                        return new Result(STOPPED, "");
                    }
                }
            } finally {
                try {
                    in.close();
                } catch (IOException e) {
                    // Already read; nothing to keep.
                }
            }
            if (length >= 0 && total != length) {
                return new Result(LOST, "the body ended after " + total + " of " + length
                        + " bytes");
            }
            return new Result(OK, "");
        } catch (IOException | RuntimeException e) {
            if (cancelled.get() || sink.cancelled()) {
                return new Result(CANCELLED, "");
            }
            return new Result(classify(e, reading), text(e));
        } finally {
            done.set(true);
            watcher.interrupt();
            try {
                watcher.join();
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            c.disconnect();
        }
    }

    /** Content-Length, or -1 when the response has none or is chunked. */
    static long contentLength(HttpURLConnection c) {
        String te = c.getHeaderField("Transfer-Encoding");
        if (te != null && te.toLowerCase(java.util.Locale.ROOT).contains("chunked")) {
            return -1;
        }
        return parseLength(c.getHeaderField("Content-Length"));
    }

    /** Digits only, at most 18 of them; anything else is -1. */
    static long parseLength(String value) {
        if (value == null) {
            return -1;
        }
        String v = value.trim();
        if (v.isEmpty() || v.length() > 18) {
            return -1;
        }
        for (int i = 0; i < v.length(); i++) {
            char ch = v.charAt(i);
            if (ch < '0' || ch > '9') {
                return -1;
            }
        }
        return Long.parseLong(v);
    }

    /**
     * What went wrong, as the core's iUpdateNet. `reading`: the response had
     * started, so a broken stream is a lost connection.
     */
    static int classify(Throwable e, boolean reading) {
        if (e instanceof SocketTimeoutException) {
            return TIMEOUT;
        }
        if (e instanceof UnknownHostException) {
            return DNS;
        }
        if (e instanceof SSLPeerUnverifiedException) {
            // The certificate is not for this host name.
            return CERT;
        }
        if (e instanceof SSLException) {
            int cert = certificateProblem(e);
            if (cert != OK) {
                return cert;
            }
            return reading ? LOST : TLS;
        }
        if (e instanceof ConnectException || e instanceof NoRouteToHostException
                || e instanceof PortUnreachableException) {
            return CONNECT;
        }
        if (e instanceof ProtocolException) {
            return reading ? LOST : PROTOCOL;
        }
        if (e instanceof IOException) {
            int cert = certificateProblem(e);
            if (cert != OK) {
                return cert;
            }
            return reading ? LOST : CONNECT;
        }
        return ERROR;
    }

    /** CLOCK or CERT when a certificate was refused somewhere in the causes, else OK. */
    static int certificateProblem(Throwable e) {
        int found = OK;
        int depth = 0;
        for (Throwable t = e; t != null && depth < 16; t = t.getCause(), depth++) {
            if (t instanceof CertificateExpiredException
                    || t instanceof CertificateNotYetValidException) {
                return CLOCK;
            }
            if (t instanceof CertPathValidatorException) {
                CertPathValidatorException.Reason r = ((CertPathValidatorException) t).getReason();
                if (r == CertPathValidatorException.BasicReason.EXPIRED
                        || r == CertPathValidatorException.BasicReason.NOT_YET_VALID) {
                    return CLOCK;
                }
                found = CERT;
            }
            if (t instanceof CertificateException) {
                found = CERT;
            }
        }
        return found;
    }

    /** The exception for the log, on one line and short. */
    static String text(Throwable e) {
        String s = e.getClass().getName();
        String m = e.getMessage();
        if (m != null && !m.isEmpty()) {
            s += ": " + m;
        }
        Throwable cause = e.getCause();
        if (cause != null && cause != e) {
            s += " (" + cause.getClass().getName() + ")";
        }
        s = s.replace('\r', ' ').replace('\n', ' ');
        return s.length() > 240 ? s.substring(0, 240) : s;
    }
}
