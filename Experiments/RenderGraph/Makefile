CXX ?= clang++
CXXFLAGS := -std=c++17 -Wall -Wextra -g

# サニタイザ付きでビルドする場合: make SAN=1
# （このマシンの macOS では ASan ランタイムが起動時に固まったので既定では無効）
ifdef SAN
CXXFLAGS += -fsanitize=address,undefined
endif

SRCS := main.cpp RenderGraph.cpp TexturePool.cpp
HDRS := RenderGraph.h CommandContext.h TexturePool.h

rgtest: $(SRCS) $(HDRS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

test: rgtest
	./rgtest

clean:
	rm -rf rgtest rgtest.dSYM rgtest_nosan rgtest_nosan.dSYM

.PHONY: test clean
