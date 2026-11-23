#!/usr/bin/env bash
# Where the oracle's checks run for this clone (sourced by session.sh, check.sh
# and rawjudge.sh; `bash host.sh` prints "HOST DIR"): home, the ssh host whose
# checkout holds the recordings and the Java client, empty when it is this
# machine; R, that checkout's directory under the remote home. From this
# clone's git config netherite.host and netherite.dir, else from an origin of
# the form HOST:DIR (a clone of the dev host's checkout, as the Mac's is), else
# this machine.
_hroot=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
home=$(git -C "$_hroot" config netherite.host 2>/dev/null)
R=$(git -C "$_hroot" config netherite.dir 2>/dev/null)
_hurl=$(git -C "$_hroot" remote get-url origin 2>/dev/null)
if [[ -z $home && $_hurl =~ ^([^@/:]+@)?([^/:]+):([^/].*)$ && ! ${BASH_REMATCH[2]} =~ (github|gitlab|bitbucket) ]]; then
    home=${BASH_REMATCH[2]}; R=${R:-${BASH_REMATCH[3]%.git}}
fi
R=${R:-dev/netherite-v2}
unset _hroot _hurl
[[ ${BASH_SOURCE[0]} != "$0" ]] || echo "${home:--} $R"
