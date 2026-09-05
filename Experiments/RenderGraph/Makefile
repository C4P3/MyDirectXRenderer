CXX ?= clang++
RG_DIR   := ../../MyDirectXRenderer/RenderGraph/Frontend
CXXFLAGS := -std=c++17 -Wall -Wextra -g -I$(RG_DIR)

SRCS := main.cpp $(RG_DIR)/RenderGraph.cpp $(RG_DIR)/TexturePool.cpp
HDRS := $(RG_DIR)/RenderGraph.h $(RG_DIR)/CommandContext.h $(RG_DIR)/TexturePool.h

# サニタイザ付きでビルドする場合: make SAN=1
# （このマシンの macOS では ASan ランタイムが起動時に固まったので既定では無効）
ifdef SAN
CXXFLAGS += -fsanitize=address,undefined
endif

rgtest: $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

test: rgtest
	./rgtest

clean:
	rm -rf rgtest rgtest.dSYM rgtest_nosan rgtest_nosan.dSYM

.PHONY: test clean
