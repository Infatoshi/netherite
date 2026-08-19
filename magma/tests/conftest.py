"""pytest options for magma/tests (portal e2e and friends)."""


def pytest_addoption(parser):
    group = parser.getgroup("portal-e2e")
    group.addoption(
        "--portal-e2e-out",
        default="/tmp/portal_e2e",
        help="artifact directory for portal e2e (default /tmp/portal_e2e)",
    )
    group.addoption(
        "--portal-e2e-reuse",
        action="store_true",
        default=False,
        help="reuse existing results.json capture and rescore only",
    )
    group.addoption(
        "--portal-e2e-allow-skip",
        action="store_true",
        default=False,
        help="skip (not fail) when qrl is not listening on 25575",
    )
