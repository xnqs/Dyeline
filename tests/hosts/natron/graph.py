# Run by NatronRenderer: reads an image, runs it through Dyeline with a look, and writes the
# result. The paths come from the environment (see run.py), since Natron passes a script no
# arguments of its own.
import os

import NatronEngine

PLUGIN = "io.github.xnqs.Dyeline"

reader = app.createReader(os.environ["DYELINE_TEST_IN"])
reader.setScriptName("TestRead")

effect = app.createNode(PLUGIN)
if effect is None:
    ids = [i for i in NatronEngine.natron.getPluginIDs() if "yeline" in i]
    raise RuntimeError("Natron did not load %s (matching plugins: %s)" % (PLUGIN, ids))
effect.setScriptName("Dyeline")
effect.connectInput(0, reader)
with open(os.environ["DYELINE_TEST_LOOK"]) as f:
    effect.getParam("look").setValue(f.read())
effect.getParam("strength").setValue(float(os.environ.get("DYELINE_TEST_STRENGTH", "1")))

writer = app.createWriter(os.environ["DYELINE_TEST_OUT"])
writer.setScriptName("TestWrite")
writer.connectInput(0, effect)
# Write at the input's size, not the project's.
writer.getParam("formatType").setValue(0)
