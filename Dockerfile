# --- Stage 1: build syfco with system GHC + cabal (small & reliable)
FROM debian:bookworm-slim AS syfco-builder
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ghc cabal-install git libgmp-dev zlib1g-dev ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# Build syfco
RUN git clone --depth 1 https://github.com/reactive-systems/syfco.git /src/syfco \
 && cd /src/syfco \
 && cabal update \
 && cabal v2-install --installdir=/out --overwrite-policy=always

# ---------- Stage 2 (same as above) ----------
FROM python:3.12-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
      mona graphviz ca-certificates && \
    rm -rf /var/lib/apt/lists/*

RUN pip install --no-cache-dir "Scarlet-ltl==0.0.4" "ltlf2dfa==1.0.1"
COPY --from=syfco-builder /out/syfco /usr/local/bin/syfco
RUN syfco -h >/dev/null && python - <<'PY'
from ltlf2dfa.parser.ltlf import LTLfParser
f = LTLfParser()("F(p)")
assert "digraph" in f.to_dfa()
print("LTLf2DFA+MONA OK")
PY
WORKDIR /work
CMD ["bash"]
