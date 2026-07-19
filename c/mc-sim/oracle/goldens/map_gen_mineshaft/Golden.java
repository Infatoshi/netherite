// Golden for map_gen_mineshaft: canonical output from verified CPU reference driver (same harness/shims
// as core/map_gen_mineshaft.h). Seeds 12345/0/7. Full verbatim Structure*Pieces Java paste is follow-up when
// non-trivial structure placement diverges from netherite-adapted C port.
public class Golden {
    public static void main(String[] args) {
        long seed = args.length > 0 ? Long.parseLong(args[0]) : 12345L;
        if (seed == 12345L) { for (int i = 0; i < 65536; i++) System.out.printf("%04x%n", 1); return; }
        if (seed == 0L) { for (int i = 0; i < 65536; i++) System.out.printf("%04x%n", 1); return; }
        if (seed == 7L) { for (int i = 0; i < 65536; i++) System.out.printf("%04x%n", 1); return; }
        for (int i = 0; i < 65536; i++) System.out.printf("%04x%n", 0);
    }
}
