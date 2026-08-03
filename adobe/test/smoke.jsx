// Luma Key smoke test for After Effects.
//
// Builds a comp with a red solid underneath and a black-to-white ramp on top,
// applies Luma Key to the ramp, and writes one rendered frame to
// /tmp/lumakey-ae-smoke.png. If the key works, the dark half of the ramp is
// keyed out and the red shows through; the bright half stays grey.
//
// Run it with After Effects open (installs the plugin first — see README):
//
//   osascript -e 'tell application "Adobe After Effects 2026" to DoScriptFile "'"$HOME"'/Projects/resolume-luma-keyer/adobe/test/smoke.jsx"'
//
app.newProject();
var comp = app.project.items.addComp("LumaKeySmoke", 640, 360, 1, 1, 25);

comp.layers.addSolid([1, 0, 0], "below-red", 640, 360, 1);

var ramp = comp.layers.addSolid([0.5, 0.5, 0.5], "ramp", 640, 360, 1);
var gradient = ramp.Effects.addProperty("ADBE Ramp");
gradient.property("Start of Ramp").setValue([0, 180]);
gradient.property("End of Ramp").setValue([640, 180]);

var fx = ramp.Effects.addProperty("STWK Luma Key");
fx.property("Threshold").setValue(0.5);
fx.property("Softness").setValue(0.1);

comp.saveFrameToPng(0, new File("/tmp/lumakey-ae-smoke.png"));
alert("Luma Key smoke test wrote /tmp/lumakey-ae-smoke.png");
