"""Read only the three MULD parents and their physical-source locators."""
import json
import sys
sys.dont_write_bytecode = True
sys.path.insert(0, '/Users/martymclean/Downloads/pyavb-main/src')
import avb
path = '/Volumes/EDIT/2017_09_09_FIXING_JAMES_KINGSLEY/01_AVID/FIXING_JAMES_KINGSLEY/01_SEQ/01_SEQ.avb'
report = []
with avb.open(path, use_ext=False) as source:
    for handle in [92874, 92880, 92886]:
        parent = source.read_object(handle)
        physical = parent.physical_media
        locator = physical.locator if physical else None
        report.append({
            'parent': handle,
            'parentClass': parent.class_id.decode(),
            'parentLocator': parent.locator.instance_id if parent.locator else None,
            'physicalObject': physical.instance_id if physical else None,
            'physicalClass': physical.class_id.decode() if physical else None,
            'physicalLocatorObject': locator.instance_id if locator else None,
            'physicalLocatorClass': locator.class_id.decode() if locator else None,
            'physicalLocatorFields': {key: str(value) for key, value in locator.property_data.items()} if locator else None,
        })
print(json.dumps({'path': path, 'parents': report}, indent=2))
