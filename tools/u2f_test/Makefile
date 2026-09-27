# U2F 認証器 (app_h533/u2f) の PC 上検証
#
#   make run   … ビルドして実行し、アテステーション証明書を openssl で検証する
#
# 要件: clang (ホスト向けビルド), openssl

CLANG   ?= clang
OPENSSL ?= openssl
ROOT    := ../..
U2F     := $(ROOT)/app_h533/u2f

SRC := u2f_test.c \
	$(U2F)/ctaphid.c $(U2F)/u2f.c $(U2F)/u2f_cert.c $(U2F)/u2f_crypto.c $(U2F)/u2f_store.c \
	$(ROOT)/device/wiznet/tk_sha256.c $(ROOT)/lib/micro-ecc/uECC.c

FLAGS := -O1 -DU2F_HOST_TEST -DMTKBSP_CPU_STM32H5 -D_CRT_SECURE_NO_WARNINGS \
	-DuECC_SUPPORTS_secp160r1=0 -DuECC_SUPPORTS_secp192r1=0 \
	-DuECC_SUPPORTS_secp224r1=0 -DuECC_SUPPORTS_secp256k1=0 \
	-include ../kb_wasm_test/shim/fake_flash.h \
	-I../kb_wasm_test/shim -I$(U2F) -I$(ROOT)/lib/micro-ecc \
	-Wno-everything

EXE := u2f_test.exe

.PHONY: all run clean

all: $(EXE)

$(EXE): $(SRC) $(wildcard $(U2F)/*.h)
	$(CLANG) $(FLAGS) -o $@ $(SRC)

run: $(EXE)
	./$(EXE)
	@echo '--- openssl: attestation certificate ---'
	$(OPENSSL) x509 -inform DER -in attestation.der -noout -subject -issuer -dates -text | head -20
	$(OPENSSL) x509 -inform DER -in attestation.der -out attestation.pem
	$(OPENSSL) verify -check_ss_sig -CAfile attestation.pem attestation.pem

clean:
	rm -f $(EXE) *.pdb *.ilk attestation.der attestation.pem
