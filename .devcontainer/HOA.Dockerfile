# ---------- Stage 1 syfco builder ----------
FROM debian:bookworm AS syfco-builder

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ghc cabal-install git libgmp-dev zlib1g-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Build syfco
RUN git clone --depth 1 https://github.com/reactive-systems/syfco.git /src/syfco \
    && cd /src/syfco \
    && cabal update \
    && cabal v2-install --installdir=/out --overwrite-policy=always

# ---------- Stage 3 runtime ----------
FROM python:3.12.3-bookworm

# base tools
RUN apt-get update && apt-get install -y --no-install-recommends \
    mona graphviz ca-certificates zsh curl git wget gnupg && \
    rm -rf /var/lib/apt/lists/*

# Python deps
RUN pip install --upgrade pip
RUN pip install --no-cache-dir Scarlet-ltl==0.0.4 ltlf2dfa==1.0.1
RUN pip install hoax-hoa-executor

# oh-my-zsh + powerlevel10k
RUN sh -c "$(curl -fsSL https://raw.githubusercontent.com/ohmyzsh/ohmyzsh/master/tools/install.sh)" "" --unattended \
    && git clone --depth=1 https://github.com/romkatv/powerlevel10k.git \
    ${ZSH_CUSTOM:-$HOME/.oh-my-zsh/custom}/themes/powerlevel10k


# bring syfco from builder
COPY --from=syfco-builder /out/syfco /usr/local/bin/syfco

# Build spot
RUN wget http://www.lrde.epita.fr/dload/spot/spot-2.14.1.tar.gz && \
    tar xzf spot-2.14.1.tar.gz && cd spot-2.14.1 && \
    ./configure --prefix /usr && make -j"$(nproc)" && make install



WORKDIR /tempo-rl
CMD [ "zsh" ]
