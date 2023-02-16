from pathlib import Path
import glob
import os

base_path = os.path.dirname(os.path.realpath(__file__))


def can_build(env, platform):
    return True


def configure(env):
    pass


def get_doc_path():
    return "gw2cc-doc"


def get_doc_classes():
    classes = []
    for cls in glob.iglob(os.path.join(base_path, "gw2cc-doc/*.xml")):
        classes.append(Path(cls).stem)
    return classes
