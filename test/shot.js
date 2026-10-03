// Capture the grow room
ares.setHomebrew(true);
ares.setRenderer("angrylion");
ares.loadRom(ares.args[0]);
ares.resume();
ares.waitFrames(60);
var s = ares.screenshot();
s.save("/home/lex/n64/weedfarmer64/shots/growroom.png");
console.log("shot ok");
