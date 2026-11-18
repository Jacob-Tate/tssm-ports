package io.github.tssmport.game;

import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpHandler;
import com.sun.net.httpserver.HttpsConfigurator;
import com.sun.net.httpserver.HttpsServer;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.ConnectException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketException;
import java.net.SocketTimeoutException;
import java.net.UnknownHostException;
import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import java.security.cert.CertPathValidatorException;
import java.security.cert.CertificateException;
import java.security.cert.CertificateExpiredException;
import java.security.cert.CertificateNotYetValidException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.TreeSet;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;

import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.KeyManagerFactory;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLException;
import javax.net.ssl.SSLHandshakeException;
import javax.net.ssl.SSLPeerUnverifiedException;
import javax.net.ssl.TrustManagerFactory;

/**
 * UpdateNet and UpdateText on a desktop JVM (JDK 17 or newer, with keytool).
 * No test framework is in the offline Gradle cache, so this is a plain
 * program (docs/ANDROID.md, "Updates"):
 *
 * <pre>
 * javac -d out android/app/src/update/java/io/github/tssmport/game/UpdateNet.java \
 *       android/app/src/update/java/io/github/tssmport/game/UpdateText.java \
 *       android/app/src/updateTest/java/io/github/tssmport/game/UpdateNetTest.java
 * java -cp out io.github.tssmport.game.UpdateNetTest &lt;scratch folder&gt; [--online owner/name]
 * </pre>
 *
 * <p>It makes a test certificate authority and server certificates with
 * keytool in the scratch folder, and runs HTTPS servers on this machine:
 * whole bodies, chunked ones, redirects (not followed), errors, the request's
 * headers, cancels, timeouts, short bodies, a certificate for another name,
 * an expired one, one from an unknown authority, and a server that does not
 * speak TLS. The client trusts the test authority only for this process
 * (HttpsURLConnection's default socket factory); UpdateNet itself sets no
 * trust of its own. With --online it also reads a repository's latest
 * release from GitHub, through the system's real trust store.
 *
 * <p>Exit status 0 when every check passed.
 */
public final class UpdateNetTest {

    private static int sChecks;
    private static int sFailed;

    private static final String PASS = "changeit";

    private static void check(boolean ok, String what) {
        sChecks++;
        if (!ok) {
            sFailed++;
            System.out.println("FAIL: " + what);
        }
    }

    private static void eq(int got, int want, String what) {
        check(got == want, what + ": got " + name(got) + ", want " + name(want));
    }

    private static String name(int code) {
        String[] names = { "OK", "OFFLINE", "DNS", "CONNECT", "TLS", "CERT", "CLOCK", "TIMEOUT",
            "LOST", "PROTOCOL", "CANCELLED", "STOPPED", "ERROR" };
        return code >= 0 && code < names.length ? names[code] : String.valueOf(code);
    }

    /** A sink that keeps what it is given. */
    private static class Keep implements UpdateNet.Sink {
        int status = -1;
        long length = -2;
        String location;
        final ByteArrayOutputStream body = new ByteArrayOutputStream();
        boolean wantBody = true;
        long stopAfter = Long.MAX_VALUE;
        volatile long cancelAt = Long.MAX_VALUE; // System.nanoTime()
        volatile long cancelledAt;

        @Override
        public boolean onResponse(int status, long contentLength, String location) {
            this.status = status;
            this.length = contentLength;
            this.location = location;
            return wantBody;
        }

        @Override
        public boolean onBody(byte[] data, int n) {
            body.write(data, 0, n);
            return body.size() <= stopAfter;
        }

        @Override
        public boolean cancelled() {
            if (System.nanoTime() >= cancelAt) {
                if (cancelledAt == 0) {
                    cancelledAt = System.nanoTime();
                }
                return true;
            }
            return false;
        }
    }

    // --- the parts that need no network ---------------------------------------

    private static void testClassify() {
        eq(UpdateNet.classify(new SocketTimeoutException("x"), false), UpdateNet.TIMEOUT,
                "timeout");
        eq(UpdateNet.classify(new UnknownHostException("x"), false), UpdateNet.DNS, "dns");
        eq(UpdateNet.classify(new ConnectException("x"), false), UpdateNet.CONNECT, "refused");
        eq(UpdateNet.classify(new SSLPeerUnverifiedException("x"), false), UpdateNet.CERT,
                "wrong host name (Android)");
        SSLHandshakeException expired = new SSLHandshakeException("x");
        expired.initCause(new CertPathValidatorException("timestamp check failed",
                new CertificateExpiredException("x")));
        eq(UpdateNet.classify(expired, false), UpdateNet.CLOCK, "expired, wrapped");
        SSLHandshakeException early = new SSLHandshakeException("x");
        early.initCause(new CertificateNotYetValidException("x"));
        eq(UpdateNet.classify(early, false), UpdateNet.CLOCK, "not yet valid");
        SSLHandshakeException reason = new SSLHandshakeException("x");
        reason.initCause(new CertPathValidatorException("x", null, null, -1,
                CertPathValidatorException.BasicReason.EXPIRED));
        eq(UpdateNet.classify(reason, false), UpdateNet.CLOCK, "expired, by reason");
        SSLHandshakeException anchor = new SSLHandshakeException("x");
        anchor.initCause(new CertificateException(
                new CertPathValidatorException("Trust anchor for certification path not found.")));
        eq(UpdateNet.classify(anchor, false), UpdateNet.CERT, "unknown authority");
        eq(UpdateNet.classify(new SSLHandshakeException("handshake failed"), false),
                UpdateNet.TLS, "handshake");
        eq(UpdateNet.classify(new SSLException("connection reset"), true), UpdateNet.LOST,
                "TLS error while reading");
        eq(UpdateNet.classify(new SocketException("reset"), true), UpdateNet.LOST,
                "reset while reading");
        eq(UpdateNet.classify(new SocketException("reset"), false), UpdateNet.CONNECT,
                "reset while connecting");
        eq(UpdateNet.classify(new java.net.ProtocolException("x"), false), UpdateNet.PROTOCOL,
                "protocol");
        eq(UpdateNet.classify(new IllegalStateException("x"), false), UpdateNet.ERROR, "other");
        check(UpdateNet.text(new IOException("a\nb")).indexOf('\n') < 0, "one line");
    }

    private static void testParseLength() {
        check(UpdateNet.parseLength("123") == 123, "length 123");
        check(UpdateNet.parseLength(" 7 ") == 7, "length with spaces");
        check(UpdateNet.parseLength(null) == -1, "no length");
        check(UpdateNet.parseLength("") == -1, "empty length");
        check(UpdateNet.parseLength("-5") == -1, "negative length");
        check(UpdateNet.parseLength("12a") == -1, "length with a letter");
        check(UpdateNet.parseLength("1234567890123456789") == -1, "length too long");
    }

    private static void testText() {
        String key = UpdateText.installFailure(UpdateText.STATUS_FAILURE_CONFLICT,
                "INSTALL_FAILED_UPDATE_INCOMPATIBLE: Package io.github.tssmport.game signatures "
                + "do not match newer version; ignoring!");
        check(key.equals(UpdateText.DIFFERENT_KEY), "different key: " + key);
        String down = UpdateText.installFailure(UpdateText.STATUS_FAILURE_INCOMPATIBLE,
                "INSTALL_FAILED_VERSION_DOWNGRADE: Downgrade detected");
        check(down.contains("lower than this copy's"), "downgrade: " + down);
        check(UpdateText.downgrade(15, 16).contains("(15)") && UpdateText.downgrade(15, 16)
                .contains("(16)"), "downgrade with numbers");
        check(UpdateText.installFailure(UpdateText.STATUS_FAILURE_ABORTED, null)
                .contains("cancelled"), "aborted");
        check(UpdateText.installFailure(UpdateText.STATUS_FAILURE_STORAGE, "x")
                .contains("storage"), "storage");
        int[] all = { UpdateText.STATUS_FAILURE, UpdateText.STATUS_FAILURE_BLOCKED,
            UpdateText.STATUS_FAILURE_ABORTED, UpdateText.STATUS_FAILURE_INVALID,
            UpdateText.STATUS_FAILURE_CONFLICT, UpdateText.STATUS_FAILURE_STORAGE,
            UpdateText.STATUS_FAILURE_INCOMPATIBLE, UpdateText.STATUS_FAILURE_TIMEOUT, 99 };
        for (int s : all) {
            String t = UpdateText.installFailure(s, null);
            check(t.endsWith(UpdateText.UNCHANGED), "status " + s + " says nothing changed");
        }
    }

    // --- certificates (keytool) -------------------------------------------------

    private static void keytool(File dir, String... args) throws Exception {
        String exe = new File(new File(System.getProperty("java.home"), "bin"),
                System.getProperty("os.name").startsWith("Windows") ? "keytool.exe" : "keytool")
                .getPath();
        List<String> cmd = new ArrayList<>();
        cmd.add(exe);
        cmd.addAll(Arrays.asList(args));
        Process p = new ProcessBuilder(cmd).directory(dir).redirectErrorStream(true).start();
        byte[] out = p.getInputStream().readAllBytes();
        if (p.waitFor() != 0) {
            throw new IOException("keytool " + String.join(" ", args) + ":\n"
                    + new String(out, StandardCharsets.UTF_8));
        }
    }

    /** A server certificate signed by the test authority, in <name>.p12. */
    private static void leaf(File dir, String name, String san, String start, int days)
            throws Exception {
        new File(dir, name + ".p12").delete();
        keytool(dir, "-genkeypair", "-alias", "leaf", "-keyalg", "EC", "-groupname", "secp256r1",
                "-dname", "CN=" + san, "-keystore", name + ".p12", "-storetype", "PKCS12",
                "-storepass", PASS, "-validity", "30");
        keytool(dir, "-certreq", "-alias", "leaf", "-keystore", name + ".p12", "-storepass", PASS,
                "-file", name + ".csr");
        List<String> gen = new ArrayList<>(Arrays.asList("-gencert", "-alias", "ca", "-keystore",
                "ca.p12", "-storepass", PASS, "-infile", name + ".csr", "-outfile",
                name + ".pem", "-rfc", "-ext", "SAN=dns:" + san, "-validity",
                String.valueOf(days)));
        if (start != null) {
            gen.add("-startdate");
            gen.add(start);
        }
        keytool(dir, gen.toArray(new String[0]));
        keytool(dir, "-importcert", "-alias", "ca", "-keystore", name + ".p12", "-storepass", PASS,
                "-file", "ca.pem", "-noprompt");
        keytool(dir, "-importcert", "-alias", "leaf", "-keystore", name + ".p12", "-storepass",
                PASS, "-file", name + ".pem", "-noprompt");
    }

    private static void makeCertificates(File dir) throws Exception {
        for (String f : new String[] { "ca.p12", "trust.p12", "untrusted.p12" }) {
            new File(dir, f).delete();
        }
        keytool(dir, "-genkeypair", "-alias", "ca", "-keyalg", "EC", "-groupname", "secp256r1",
                "-dname", "CN=updater test authority", "-ext", "bc:c", "-validity", "30",
                "-keystore", "ca.p12", "-storetype", "PKCS12", "-storepass", PASS);
        keytool(dir, "-exportcert", "-alias", "ca", "-keystore", "ca.p12", "-storepass", PASS,
                "-rfc", "-file", "ca.pem");
        keytool(dir, "-importcert", "-alias", "ca", "-file", "ca.pem", "-keystore", "trust.p12",
                "-storetype", "PKCS12", "-storepass", PASS, "-noprompt");
        leaf(dir, "good", "localhost", null, 30);
        leaf(dir, "othername", "example.invalid", null, 30);
        leaf(dir, "expired", "localhost", "-10d", 2);
        keytool(dir, "-genkeypair", "-alias", "leaf", "-keyalg", "EC", "-groupname", "secp256r1",
                "-dname", "CN=localhost", "-ext", "SAN=dns:localhost", "-validity", "30",
                "-keystore", "untrusted.p12", "-storetype", "PKCS12", "-storepass", PASS);
    }

    private static KeyStore load(File f) throws Exception {
        KeyStore ks = KeyStore.getInstance("PKCS12");
        try (InputStream in = new FileInputStream(f)) {
            ks.load(in, PASS.toCharArray());
        }
        return ks;
    }

    private static SSLContext serverContext(File p12) throws Exception {
        KeyManagerFactory kmf = KeyManagerFactory.getInstance(
                KeyManagerFactory.getDefaultAlgorithm());
        kmf.init(load(p12), PASS.toCharArray());
        SSLContext ctx = SSLContext.getInstance("TLS");
        ctx.init(kmf.getKeyManagers(), null, null);
        return ctx;
    }

    // --- servers ----------------------------------------------------------------

    private static final InetAddress LOCALHOST;

    static {
        try {
            LOCALHOST = InetAddress.getByName("localhost");
        } catch (UnknownHostException e) {
            throw new ExceptionInInitializerError(e);
        }
    }

    private static final Map<String, Map<String, List<String>>> sSeenHeaders =
            new ConcurrentHashMap<>();
    private static final AtomicInteger sTargetHits = new AtomicInteger();

    private static byte[] bytes(int n) {
        byte[] b = new byte[n];
        for (int i = 0; i < n; i++) {
            b[i] = (byte) (i * 31 + 7);
        }
        return b;
    }

    private static void sleep(long ms) {
        try {
            Thread.sleep(ms);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    private static HttpsServer server(File p12, ExecutorService pool) throws Exception {
        HttpsServer s = HttpsServer.create(new InetSocketAddress(LOCALHOST, 0), 16);
        s.setHttpsConfigurator(new HttpsConfigurator(serverContext(p12)));
        s.setExecutor(pool);
        s.createContext("/", new HttpHandler() {
            @Override
            public void handle(HttpExchange ex) throws IOException {
                String path = ex.getRequestURI().getPath();
                sSeenHeaders.put(path, new java.util.HashMap<>(ex.getRequestHeaders()));
                try {
                    respond(ex, path);
                } catch (IOException e) {
                    // The client went away: the cancel tests.
                } finally {
                    ex.close();
                }
            }
        });
        s.start();
        return s;
    }

    private static void respond(HttpExchange ex, String path) throws IOException {
        switch (path) {
            case "/ok": {
                byte[] b = "hello".getBytes(StandardCharsets.UTF_8);
                ex.sendResponseHeaders(200, b.length);
                ex.getResponseBody().write(b);
                return;
            }
            case "/chunked": {
                ex.sendResponseHeaders(200, 0);
                OutputStream out = ex.getResponseBody();
                for (int i = 0; i < 300; i++) {
                    out.write(bytes(1000));
                }
                return;
            }
            case "/redirect": {
                ex.getResponseHeaders().add("Location",
                        "https://release-assets.githubusercontent.com/x?sig=abc");
                ex.sendResponseHeaders(302, -1);
                return;
            }
            case "/target": {
                sTargetHits.incrementAndGet();
                ex.sendResponseHeaders(200, -1);
                return;
            }
            case "/missing": {
                byte[] b = "{\"message\":\"Not Found\"}".getBytes(StandardCharsets.UTF_8);
                ex.sendResponseHeaders(404, b.length);
                ex.getResponseBody().write(b);
                return;
            }
            case "/big": {
                byte[] b = bytes(3 * 1024 * 1024);
                ex.sendResponseHeaders(200, b.length);
                ex.getResponseBody().write(b);
                return;
            }
            case "/slow": {
                // 100 bytes every 50 ms, for 10 seconds.
                ex.sendResponseHeaders(200, 20_000);
                OutputStream out = ex.getResponseBody();
                for (int i = 0; i < 200; i++) {
                    out.write(bytes(100));
                    out.flush();
                    sleep(50);
                }
                return;
            }
            case "/stall": {
                ex.sendResponseHeaders(200, 1_000_000);
                OutputStream out = ex.getResponseBody();
                out.write(bytes(1000));
                out.flush();
                sleep(3000);
                out.write(bytes(999_000));
                return;
            }
            case "/hang": {
                sleep(4000);
                ex.sendResponseHeaders(200, -1);
                return;
            }
            case "/short": {
                ex.sendResponseHeaders(200, 1000);
                OutputStream out = ex.getResponseBody();
                out.write(bytes(10));
                out.flush();
                // The connection ends here, 990 bytes short.
                throw new IOException("cut short on purpose");
            }
            default:
                ex.sendResponseHeaders(500, -1);
        }
    }

    private static UpdateNet.Result get(int port, String path, Keep sink, int timeoutMs) {
        return UpdateNet.get("localhost", port, path, "application/octet-stream", timeoutMs, sink);
    }

    private static void testServer(File dir) throws Exception {
        ExecutorService pool = Executors.newCachedThreadPool();
        HttpsServer good = server(new File(dir, "good.p12"), pool);
        HttpsServer other = server(new File(dir, "othername.p12"), pool);
        HttpsServer expired = server(new File(dir, "expired.p12"), pool);
        HttpsServer untrusted = server(new File(dir, "untrusted.p12"), pool);
        int port = good.getAddress().getPort();
        try {
            // A whole body, with its length.
            Keep k = new Keep();
            UpdateNet.Result r = get(port, "/ok", k, 5000);
            eq(r.code, UpdateNet.OK, "/ok");
            check(k.status == 200 && k.length == 5 && "hello".equals(k.body.toString("UTF-8")),
                    "/ok body: " + k.status + " " + k.length);

            // The request's headers: these and no others.
            Map<String, List<String>> h = sSeenHeaders.get("/ok");
            TreeSet<String> names = new TreeSet<>();
            for (String n : h.keySet()) {
                names.add(n.toLowerCase(java.util.Locale.ROOT));
            }
            check(names.equals(new TreeSet<>(Arrays.asList("host", "user-agent", "accept",
                    "accept-encoding", "connection"))), "request headers: " + names);
            check(h.get("User-agent") != null
                    && h.get("User-agent").equals(Arrays.asList(UpdateNet.USER_AGENT)),
                    "user agent: " + h.get("User-agent"));
            check(Arrays.asList("application/octet-stream").equals(h.get("Accept")),
                    "accept: " + h.get("Accept"));
            check(Arrays.asList("identity").equals(h.get("Accept-encoding")),
                    "accept-encoding: " + h.get("Accept-encoding"));
            check(Arrays.asList("close").equals(h.get("Connection")),
                    "connection: " + h.get("Connection"));

            // Chunked: no length, the whole body.
            k = new Keep();
            r = get(port, "/chunked", k, 5000);
            eq(r.code, UpdateNet.OK, "/chunked");
            check(k.length == -1 && k.body.size() == 300_000,
                    "/chunked: length " + k.length + ", " + k.body.size() + " bytes");

            // A redirect is reported, not followed.
            sTargetHits.set(0);
            k = new Keep();
            k.wantBody = false;
            r = get(port, "/redirect", k, 5000);
            eq(r.code, UpdateNet.OK, "/redirect");
            check(k.status == 302 && "https://release-assets.githubusercontent.com/x?sig=abc"
                    .equals(k.location), "/redirect: " + k.status + " " + k.location);
            check(sTargetHits.get() == 0, "the redirect was not followed");

            // An error status comes to the sink, with its body.
            k = new Keep();
            r = get(port, "/missing", k, 5000);
            eq(r.code, UpdateNet.OK, "/missing");
            check(k.status == 404 && k.body.size() > 0, "/missing: " + k.status);

            // The sink does not want the body.
            k = new Keep();
            k.wantBody = false;
            r = get(port, "/big", k, 5000);
            eq(r.code, UpdateNet.OK, "/big unwanted");
            check(k.status == 200 && k.body.size() == 0, "/big unwanted: nothing read");

            // The sink stops it (too big).
            k = new Keep();
            k.stopAfter = 1024 * 1024;
            r = get(port, "/big", k, 5000);
            eq(r.code, UpdateNet.STOPPED, "/big stopped");

            // A cancel while the body comes in, noticed within a quarter second.
            k = new Keep();
            k.cancelAt = System.nanoTime() + 500_000_000L;
            long t0 = System.nanoTime();
            r = get(port, "/slow", k, 20000);
            long tookMs = (System.nanoTime() - t0) / 1_000_000;
            eq(r.code, UpdateNet.CANCELLED, "/slow cancelled");
            long afterMs = k.cancelledAt > 0 ? (System.nanoTime() - k.cancelledAt) / 1_000_000 : -1;
            check(tookMs < 2000 && afterMs >= 0 && afterMs < 250,
                    "/slow: cancelled after " + tookMs + " ms, returned " + afterMs
                    + " ms after the cancel");

            // A cancel while a read waits for bytes that do not come. Android's
            // disconnect closes the socket under the read; desktop Java's waits
            // for the read to return, so here only the result is checked.
            k = new Keep();
            k.cancelAt = System.nanoTime() + 300_000_000L;
            t0 = System.nanoTime();
            r = get(port, "/stall", k, 20000);
            tookMs = (System.nanoTime() - t0) / 1_000_000;
            eq(r.code, UpdateNet.CANCELLED, "/stall cancelled");
            System.out.println("/stall: the cancel returned after " + tookMs
                    + " ms (desktop Java waits for the server's next bytes)");

            // A cancel before the response.
            k = new Keep();
            k.cancelAt = System.nanoTime() + 300_000_000L;
            t0 = System.nanoTime();
            r = get(port, "/hang", k, 20000);
            tookMs = (System.nanoTime() - t0) / 1_000_000;
            eq(r.code, UpdateNet.CANCELLED, "/hang cancelled");
            check(tookMs < 1500, "/hang: cancelled after " + tookMs + " ms");

            // Cancelled before it starts.
            k = new Keep();
            k.cancelAt = 0;
            eq(get(port, "/ok", k, 5000).code, UpdateNet.CANCELLED, "cancelled at once");
            check(k.status == -1, "no request once cancelled");

            // Nothing for the timeout.
            k = new Keep();
            t0 = System.nanoTime();
            r = get(port, "/hang", k, 500);
            tookMs = (System.nanoTime() - t0) / 1_000_000;
            eq(r.code, UpdateNet.TIMEOUT, "/hang timeout");
            check(tookMs < 3000, "/hang: timed out after " + tookMs + " ms");

            // A body shorter than its length.
            k = new Keep();
            r = get(port, "/short", k, 5000);
            eq(r.code, UpdateNet.LOST, "/short (" + r.detail + ")");

            // Certificates.
            k = new Keep();
            r = get(other.getAddress().getPort(), "/ok", k, 5000);
            eq(r.code, UpdateNet.CERT, "a certificate for another name (" + r.detail + ")");
            check(k.status == -1, "nothing read from the wrong name");
            k = new Keep();
            r = get(expired.getAddress().getPort(), "/ok", k, 5000);
            eq(r.code, UpdateNet.CLOCK, "an expired certificate (" + r.detail + ")");
            k = new Keep();
            r = get(untrusted.getAddress().getPort(), "/ok", k, 5000);
            eq(r.code, UpdateNet.CERT, "an unknown authority (" + r.detail + ")");
            check(k.status == -1, "nothing read from an unknown authority");
        } finally {
            good.stop(0);
            other.stop(0);
            expired.stop(0);
            untrusted.stop(0);
            pool.shutdownNow();
        }

        // Not TLS at all.
        try (ServerSocket plain = new ServerSocket(0, 4, LOCALHOST)) {
            Thread t = new Thread(new Runnable() {
                @Override
                public void run() {
                    try (Socket s = plain.accept()) {
                        s.getOutputStream().write(
                                "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n".getBytes(
                                        StandardCharsets.US_ASCII));
                        sleep(500);
                    } catch (IOException e) {
                        // The client gave up.
                    }
                }
            });
            t.start();
            UpdateNet.Result r = get(plain.getLocalPort(), "/ok", new Keep(), 5000);
            eq(r.code, UpdateNet.TLS, "a server without TLS (" + r.detail + ")");
            t.join();
        }

        // Nobody listening.
        int closed;
        try (ServerSocket s = new ServerSocket(0, 1, LOCALHOST)) {
            closed = s.getLocalPort();
        }
        UpdateNet.Result r = get(closed, "/ok", new Keep(), 5000);
        eq(r.code, UpdateNet.CONNECT, "connection refused (" + r.detail + ")");

        // A name that does not resolve.
        r = UpdateNet.get("nonexistent.invalid", 443, "/", "*/*", 5000, new Keep());
        eq(r.code, UpdateNet.DNS, "unknown host (" + r.detail + ")");
    }

    /** The real thing: GitHub's API and a release file's redirect, through the JVM's own roots. */
    private static void testOnline(String repo) throws Exception {
        Keep k = new Keep();
        UpdateNet.Result r = UpdateNet.get("api.github.com", 443,
                "/repos/" + repo + "/releases/latest", "application/vnd.github+json", 20000, k);
        eq(r.code, UpdateNet.OK, "online: the latest release (" + r.detail + ")");
        String json = k.body.toString("UTF-8");
        check(k.status == 200 && json.startsWith("{") && json.contains("\"assets\""),
                "online: HTTP " + k.status + ", " + json.length() + " bytes of JSON");
        int at = json.indexOf("\"browser_download_url\": \"https://github.com/");
        if (at < 0) {
            at = json.indexOf("\"browser_download_url\":\"https://github.com/");
        }
        check(at >= 0, "online: a download link");
        if (at < 0) {
            return;
        }
        int start = json.indexOf("https://github.com/", at);
        int end = json.indexOf('"', start);
        String url = json.substring(start, end);
        String path = url.substring("https://github.com".length());
        k = new Keep();
        k.wantBody = false;
        r = UpdateNet.get("github.com", 443, path, "application/octet-stream", 20000, k);
        eq(r.code, UpdateNet.OK, "online: " + path);
        String host = k.location != null ? java.net.URI.create(k.location).getHost() : "";
        check(k.status == 302 && (host.equals("release-assets.githubusercontent.com")
                || host.equals("objects.githubusercontent.com")),
                "online: HTTP " + k.status + " to " + host);
        System.out.println("online: " + repo + " latest release read; " + path + " -> " + host);
    }

    public static void main(String[] args) throws Exception {
        File dir = new File(args.length > 0 && !args[0].startsWith("--") ? args[0]
                : System.getProperty("java.io.tmpdir") + "/tssm-updatenet-test");
        if (!dir.isDirectory() && !dir.mkdirs()) {
            throw new IOException("cannot make " + dir);
        }
        String online = null;
        for (int i = 0; i + 1 < args.length; i++) {
            if (args[i].equals("--online")) {
                online = args[i + 1];
            }
        }

        testClassify();
        testParseLength();
        testText();

        if (online != null) {
            // Before the test authority replaces the default trust.
            testOnline(online);
        }

        makeCertificates(dir);
        TrustManagerFactory tmf = TrustManagerFactory.getInstance(
                TrustManagerFactory.getDefaultAlgorithm());
        tmf.init(load(new File(dir, "trust.p12")));
        SSLContext client = SSLContext.getInstance("TLS");
        client.init(null, tmf.getTrustManagers(), null);
        HttpsURLConnection.setDefaultSSLSocketFactory(client.getSocketFactory());
        testServer(dir);

        System.out.println(sChecks + " checks, " + sFailed + " failed");
        System.exit(sFailed == 0 ? 0 : 1);
    }
}
