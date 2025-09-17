# ---------- Stage 1 syfco builder ----------
FROM debian:bullseye-slim AS syfco-builder

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ghc cabal-install git libgmp-dev zlib1g-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Build syfco
RUN git clone --depth 1 https://github.com/reactive-systems/syfco.git /src/syfco \
    && cd /src/syfco \
    && cabal update \
    && cabal v2-install --installdir=/out --overwrite-policy=always

# ---------- Stage 2 spot builder ----------
FROM debian:bullseye-slim as spot-builder
RUN apt-get update && apt-get install -y --no-install-recommends\
    build-essential pkg-config python3-dev bison flex g++ libpopt-dev libbdd-dev graphviz git wget && rm -rf /var/lib/apt/lists/*
RUN wget http://www.lrde.epita.fr/dload/spot/spot-2.14.1.tar.gz && \
    tar xzf spot-2.14.1.tar.gz && cd spot-2.14.1 && \
    ./configure --prefix=/usr && make -j"$(nproc)" && make install


# Get Hoax and have a better understanding of what the causality stuff is doing

# ---------- Stage 3 runtime ----------
FROM python:3.12.3-slim-bookworm

# base tools
RUN apt-get update && apt-get install -y --no-install-recommends \
    mona graphviz ca-certificates zsh curl git wget gnupg && \
    rm -rf /var/lib/apt/lists/*

# Python deps
RUN pip install --upgrade pip
RUN pip install --no-cache-dir Scarlet-ltl==0.0.4 ltlf2dfa==1.0.1
RUN pip install hoax-hoa-executor


# ML deps


# oh-my-zsh + powerlevel10k
RUN sh -c "$(curl -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh)" "" --unattended \
    && git clone --depth=1 https://github.com/romkatv/powerlevel10k.git \
    ${ZSH_CUSTOM:-$HOME/.oh-my-zsh/custom}/themes/powerlevel10k


# bring syfco from builder
COPY --from=syfco-builder /out/syfco /usr/local/bin/syfco
COPY --from=spot-builder /usr/bin/ltl2tgba /usr/bin/
COPY --from=spot-builder /usr/bin/autfilt /usr/bin/
COPY --from=spot-builder /usr/lib /usr/lib

WORKDIR /app
CMD [ "zsh" ]
