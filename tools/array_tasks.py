"""Load the reachability subset of the official Arrays.set definition."""

from pathlib import Path

import yaml

from tools.run_array_fpi import sha256


def arrays_definition(svcomp, definition=None):
    directory = Path(svcomp).resolve() / 'c'
    if definition is None:
        filtered = directory / 'Arrays.no-bitwise.set'
        return filtered if filtered.is_file() else directory / 'Arrays.set'
    if Path(definition).is_absolute() or '..' in Path(definition).parts:
        raise ValueError(f'unsafe array task definition: {definition}')
    return directory / definition


def load_arrays(svcomp, *, definition=None):
    directory = Path(svcomp).resolve() / 'c'
    definition = arrays_definition(svcomp, definition)
    metadata_files = set()
    for line in definition.read_text().splitlines():
        pattern = line.split('#', 1)[0].strip()
        if not pattern:
            continue
        if Path(pattern).is_absolute() or '..' in Path(pattern).parts:
            raise ValueError(f'unsafe {definition.name} pattern: {pattern}')
        matches = sorted(directory.glob(pattern))
        if not matches:
            raise ValueError(f'missing {definition.name} inputs: {pattern}')
        metadata_files.update(matches)
    tasks = []
    for metadata in sorted(metadata_files):
        document = yaml.safe_load(metadata.read_text())
        properties = [prop for prop in document.get('properties', [])
                      if Path(prop['property_file']).name == 'unreach-call.prp']
        if not properties:
            continue
        if len(properties) != 1:
            raise ValueError(f'ambiguous reachability property: {metadata}')
        prop = properties[0]
        expected = prop.get('expected_verdict')
        if expected is not None and type(expected) is not bool:
            raise ValueError(f'invalid reference verdict: {metadata}')
        inputs = document['input_files']
        inputs = inputs if isinstance(inputs, list) else [inputs]
        sources = [path.resolve() for pattern in inputs for path in metadata.parent.glob(pattern)]
        if len(sources) != 1 or not sources[0].is_file():
            raise ValueError(f'expected one C input: {metadata}')
        property_file = (metadata.parent / prop['property_file']).resolve()
        if not property_file.is_file():
            raise ValueError(f'missing property file: {property_file}')
        model = document.get('options', {}).get('data_model', 'ILP32')
        if model not in {'ILP32', 'LP64'}:
            raise ValueError(f'unsupported data model: {metadata}')
        relative = metadata.relative_to(directory)
        name = '__'.join(relative.with_suffix('').parts)
        tasks.append(dict(name=name, family=relative.parts[0],
                          relative_metadata=relative.as_posix(), metadata=str(metadata),
                          source=str(sources[0]), property_file=str(property_file),
                          expected=expected, data_model=model,
                          source_sha256=sha256(sources[0]), metadata_sha256=sha256(metadata),
                          property_sha256=sha256(property_file)))
    if not tasks:
        raise ValueError('no Arrays.set reachability tasks found')
    if len({task['name'] for task in tasks}) != len(tasks):
        raise ValueError('duplicate Arrays.set task identifiers')
    return tasks
