.PHONY: all

all:
	cat js/jittertrap-globals.js > jittertrap-concat.js
	cat jittertrap.js >> jittertrap-concat.js
	cat js/jittertrap-utils.js >> jittertrap-concat.js
	cat js/jittertrap-websocket.js >> jittertrap-concat.js
	cat js/jittertrap-charting.js >> jittertrap-concat.js
	cat js/jittertrap-traps.js >> jittertrap-concat.js

clean:
	rm jittertrap-concat.js
