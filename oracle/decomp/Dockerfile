# MCP 9.08's toolchain (its scripts are python2). Pinned so the decompile is
# the same text on every machine: the base image by digest (its JDK 8 runs
# fernflower) and astyle, which lays out every line, by version.
FROM eclipse-temurin:8-jdk-focal@sha256:24dd0ef75eadf6107a0620471ce106d5df9716a2d4d54f2ac0f7cc16b144d823
RUN apt-get update && apt-get install -y --no-install-recommends \
      python2 python2-dev patch diffutils astyle=3.1-2build1 unzip curl ca-certificates \
 && rm -rf /var/lib/apt/lists/* \
 && ln -sf /usr/bin/python2 /usr/bin/python
WORKDIR /mcp
