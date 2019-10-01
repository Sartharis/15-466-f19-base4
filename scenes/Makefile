
BLENDER="C:\Program Files\Blender Foundation\Blender\blender.exe"

all : \
	fly-parts.pnct \
	fly-level-1.scene \

fly-parts.pnct : fly.blend export-meshes.py
	$(BLENDER) --background --python export-meshes.py -- fly.blend:Parts fly-parts.pnct

fly-level-1.scene : fly.blend export-scene.py
	$(BLENDER) --background --python export-scene.py -- fly.blend:Level.1 fly-level-1.scene
