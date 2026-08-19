package netherite.fetchassets;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Minimal JSON parser for Mojang manifests. Unknown object fields are retained
 * as map entries. Java 8 stdlib only.
 */
final class Json {
    private final String s;
    private int i;

    private Json(String s) {
        this.s = s;
        this.i = 0;
    }

    static Object parse(String text) {
        if (text == null) {
            throw new IllegalArgumentException("null JSON");
        }
        Json p = new Json(text);
        Object v = p.parseValue();
        p.skipWs();
        if (p.i != p.s.length()) {
            throw p.err("trailing input");
        }
        return v;
    }

    @SuppressWarnings("unchecked")
    static Map<String, Object> asObject(Object v) {
        if (!(v instanceof Map)) {
            throw new IllegalArgumentException("expected JSON object, got " + typeName(v));
        }
        return (Map<String, Object>) v;
    }

    @SuppressWarnings("unchecked")
    static List<Object> asArray(Object v) {
        if (!(v instanceof List)) {
            throw new IllegalArgumentException("expected JSON array, got " + typeName(v));
        }
        return (List<Object>) v;
    }

    static String asString(Object v) {
        if (!(v instanceof String)) {
            throw new IllegalArgumentException("expected JSON string, got " + typeName(v));
        }
        return (String) v;
    }

    static String typeName(Object v) {
        if (v == null) {
            return "null";
        }
        return v.getClass().getSimpleName();
    }

    private Object parseValue() {
        skipWs();
        if (i >= s.length()) {
            throw err("unexpected end");
        }
        char c = s.charAt(i);
        if (c == '{') {
            return parseObject();
        }
        if (c == '[') {
            return parseArray();
        }
        if (c == '"') {
            return parseString();
        }
        if (c == 't' || c == 'f') {
            return parseBool();
        }
        if (c == 'n') {
            return parseNull();
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return parseNumber();
        }
        throw err("unexpected '" + c + "'");
    }

    private Map<String, Object> parseObject() {
        expect('{');
        Map<String, Object> m = new LinkedHashMap<String, Object>();
        skipWs();
        if (peek('}')) {
            i++;
            return m;
        }
        while (true) {
            skipWs();
            if (i >= s.length() || s.charAt(i) != '"') {
                throw err("expected string key");
            }
            String key = parseString();
            skipWs();
            expect(':');
            Object val = parseValue();
            m.put(key, val);
            skipWs();
            if (peek('}')) {
                i++;
                return m;
            }
            expect(',');
        }
    }

    private List<Object> parseArray() {
        expect('[');
        List<Object> a = new ArrayList<Object>();
        skipWs();
        if (peek(']')) {
            i++;
            return a;
        }
        while (true) {
            a.add(parseValue());
            skipWs();
            if (peek(']')) {
                i++;
                return a;
            }
            expect(',');
        }
    }

    private String parseString() {
        expect('"');
        StringBuilder sb = new StringBuilder();
        while (i < s.length()) {
            char c = s.charAt(i++);
            if (c == '"') {
                return sb.toString();
            }
            if (c == '\\') {
                if (i >= s.length()) {
                    throw err("unterminated escape");
                }
                char e = s.charAt(i++);
                switch (e) {
                    case '"':
                    case '\\':
                    case '/':
                        sb.append(e);
                        break;
                    case 'b':
                        sb.append('\b');
                        break;
                    case 'f':
                        sb.append('\f');
                        break;
                    case 'n':
                        sb.append('\n');
                        break;
                    case 'r':
                        sb.append('\r');
                        break;
                    case 't':
                        sb.append('\t');
                        break;
                    case 'u':
                        if (i + 4 > s.length()) {
                            throw err("bad unicode escape");
                        }
                        int cp = 0;
                        for (int k = 0; k < 4; k++) {
                            char h = s.charAt(i++);
                            cp <<= 4;
                            if (h >= '0' && h <= '9') {
                                cp |= h - '0';
                            } else if (h >= 'a' && h <= 'f') {
                                cp |= h - 'a' + 10;
                            } else if (h >= 'A' && h <= 'F') {
                                cp |= h - 'A' + 10;
                            } else {
                                throw err("bad unicode hex");
                            }
                        }
                        sb.append((char) cp);
                        break;
                    default:
                        throw err("bad escape \\" + e);
                }
            } else if (c < 0x20) {
                throw err("unescaped control char");
            } else {
                sb.append(c);
            }
        }
        throw err("unterminated string");
    }

    private Boolean parseBool() {
        if (match("true")) {
            return Boolean.TRUE;
        }
        if (match("false")) {
            return Boolean.FALSE;
        }
        throw err("expected true/false");
    }

    private Object parseNull() {
        if (match("null")) {
            return null;
        }
        throw err("expected null");
    }

    private Number parseNumber() {
        int start = i;
        if (peek('-')) {
            i++;
        }
        if (i >= s.length()) {
            throw err("bad number");
        }
        if (s.charAt(i) == '0') {
            i++;
        } else if (s.charAt(i) >= '1' && s.charAt(i) <= '9') {
            while (i < s.length() && s.charAt(i) >= '0' && s.charAt(i) <= '9') {
                i++;
            }
        } else {
            throw err("bad number");
        }
        boolean frac = false;
        if (peek('.')) {
            frac = true;
            i++;
            int d0 = i;
            while (i < s.length() && s.charAt(i) >= '0' && s.charAt(i) <= '9') {
                i++;
            }
            if (i == d0) {
                throw err("bad number fraction");
            }
        }
        if (peek('e') || peek('E')) {
            frac = true;
            i++;
            if (peek('+') || peek('-')) {
                i++;
            }
            int d0 = i;
            while (i < s.length() && s.charAt(i) >= '0' && s.charAt(i) <= '9') {
                i++;
            }
            if (i == d0) {
                throw err("bad number exponent");
            }
        }
        String n = s.substring(start, i);
        try {
            if (!frac) {
                long lv = Long.parseLong(n);
                if (lv >= Integer.MIN_VALUE && lv <= Integer.MAX_VALUE) {
                    return Integer.valueOf((int) lv);
                }
                return Long.valueOf(lv);
            }
            return Double.valueOf(n);
        } catch (NumberFormatException e) {
            throw err("bad number: " + n);
        }
    }

    private void skipWs() {
        while (i < s.length()) {
            char c = s.charAt(i);
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                i++;
            } else {
                break;
            }
        }
    }

    private boolean peek(char c) {
        return i < s.length() && s.charAt(i) == c;
    }

    private void expect(char c) {
        skipWs();
        if (i >= s.length() || s.charAt(i) != c) {
            throw err("expected '" + c + "'");
        }
        i++;
    }

    private boolean match(String lit) {
        if (s.startsWith(lit, i)) {
            i += lit.length();
            return true;
        }
        return false;
    }

    private IllegalArgumentException err(String msg) {
        return new IllegalArgumentException("JSON at " + i + ": " + msg);
    }
}
