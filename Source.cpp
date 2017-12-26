#include "actor.h"
#include <iostream>
#include "stopwatch.h"
#include <stdio.h>
#include <string>
#include <inttypes.h>

class Hello : public Actor<> {
public:
	Hello() : cnt(0), lastTime(0){}
	void onMessage(const std::string& sourceName, const std::string& messageName, const std::string& msg) override {
		if (messageName == "perf") {
			auto time = w.ElapsedMilliseconds();
			if (time != 0) {
				printf("%s: qps %.2f lastTime: %llu\n", id().c_str(), float(cnt * 1000) / time, lastTime);
			} else {
				printf("%s: msgCnt: %" PRId64 ", time: %" PRId64 "\n", id().c_str(), cnt.load(), time);
			}
			return;
		}
		sendMessage(messageName, messageName, NULL);
		if (!w.IsRunning()) {
			w.Start();
		}
		lastTime = time(NULL);
		++cnt;
	}
private:
	std::atomic<uint64_t> cnt;
	Stopwatch w;
	time_t lastTime;
};

class World : public Actor<> {
public:
	World() {}
	void onMessage(const std::string& sourceName, const std::string& messageName, const std::string& msg) override {
		sendMessage(sourceName, messageName, NULL);
	}
};

int main() {
	_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG | _CRTDBG_LEAK_CHECK_DF);
	ActorManager<> inst;
	inst.registerActor("Hello1", new Hello);
	inst.registerActor("Hello2", new Hello);
	inst.registerActor("Hello3", new Hello);
	inst.registerActor("Hello4", new Hello);
	inst.registerActor("World1", new World);
	inst.registerActor("World2", new World);
	inst.registerActor("World3", new World);
	inst.registerActor("World4", new World);
	while(true) {
		std::string cmd;
		std::cin >> cmd;
		if (cmd == "exit") {
			break;
		} else if (cmd == "test") {
			std::string src, dst;
			std::cin >> src >> dst;
			inst.sendMessage("Console", src, dst, NULL);
		} else if (cmd == "perf") {
			inst.sendMessage("Console", "Hello1", "perf", NULL);
			inst.sendMessage("Console", "Hello2", "perf", NULL);
			inst.sendMessage("Console", "Hello3", "perf", NULL);
			inst.sendMessage("Console", "Hello4", "perf", NULL);
		} else if (cmd == "del") {
			std::cin >> cmd;
			inst.releaseActor(cmd);
		}
	}
	return 0;
}