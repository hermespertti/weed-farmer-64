// Verify shop open/close edges + capture mid-visit
ares.setHomebrew(true);
ares.setRenderer("angrylion");
ares.loadRom(ares.args[0]);
ares.resume();
ares.waitFrames(450);
var s = ares.screenshot();
s.save("/home/lex/n64/weedfarmer64/shots/shop.png");
ares.waitFrames(200);
console.log("shopshot ok");
