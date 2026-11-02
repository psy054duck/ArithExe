from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from xml.etree import ElementTree as ET

import yaml

from tools.array_tasks import load_arrays
from tools.run_benchexec import integer_tasks, read_results


class ArrayTaskTests(unittest.TestCase):
    def make_task(self, root, family, stem='same', reachability=True):
        folder = root/'c'/family
        folder.mkdir(parents=True, exist_ok=True)
        (folder/(stem+'.c')).write_text('int main(void) { return 0; }\n')
        prop = root/'c/properties/unreach-call.prp'
        prop.parent.mkdir(parents=True, exist_ok=True)
        prop.write_text('CHECK( init(main()), LTL(G ! call(reach_error())) )\n')
        document = dict(format_version='2.0', input_files=stem+'.c',
                        properties=[dict(property_file='../properties/'+
                                         ('unreach-call.prp' if reachability else 'valid-memsafety.prp'),
                                         expected_verdict=True)],
                        options=dict(language='C', data_model='LP64'))
        metadata = folder/(stem+'.yml')
        metadata.write_text(yaml.safe_dump(document))
        return metadata

    def test_loads_every_family_and_avoids_duplicate_basenames(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            for family in ('array-fpi', 'array-examples'):
                self.make_task(root, family)
            (root/'c/Arrays.set').write_text('# category\narray-*/*.yml\narray-fpi/*.yml # overlap\n')
            tasks = load_arrays(root)
            self.assertEqual([task['name'] for task in tasks],
                             ['array-examples__same', 'array-fpi__same'])
            self.assertTrue(all(task['data_model'] == 'LP64' for task in tasks))
            self.assertTrue(all(len(task['source_sha256']) == 64 for task in tasks))

    def test_filters_other_properties_without_dropping_reachability(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.make_task(root, 'a')
            self.make_task(root, 'a', 'memory', reachability=False)
            (root/'c/Arrays.set').write_text('a/*.yml\n')
            self.assertEqual(len(load_arrays(root)), 1)

    def test_prefers_filtered_set_without_changing_original(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.make_task(root, 'a', 'integer')
            self.make_task(root, 'a', 'bitwise')
            original = root/'c/Arrays.set'
            original.write_text('a/*.yml\n')
            (root/'c/Arrays.no-bitwise.set').write_text('a/integer.yml\n')
            self.assertEqual([task['name'] for task in load_arrays(root)],
                             ['a__integer'])
            self.assertEqual(len(load_arrays(root, definition='Arrays.set')), 2)
            self.assertEqual(original.read_text(), 'a/*.yml\n')

    def test_rejects_escaping_definition(self):
        with self.assertRaisesRegex(ValueError, 'unsafe array task definition'):
            load_arrays('/tmp', definition='../other.set')

    def test_missing_family_is_not_silently_skipped(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.make_task(root, 'a')
            (root/'c/Arrays.set').write_text('a/*.yml\nmissing/*.yml\n')
            with self.assertRaisesRegex(ValueError, 'missing Arrays.set inputs'):
                load_arrays(root)

    def test_rejects_escaping_set_pattern(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root/'c').mkdir()
            (root/'c/Arrays.set').write_text('../other/*.yml\n')
            with self.assertRaisesRegex(ValueError, 'unsafe'):
                load_arrays(root)

    def test_unknown_reference_verdict_is_retained_as_unknown(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            metadata = self.make_task(root, 'a')
            document = yaml.safe_load(metadata.read_text())
            document['properties'][0].pop('expected_verdict')
            metadata.write_text(yaml.safe_dump(document))
            (root/'c/Arrays.set').write_text('a/*.yml\n')
            self.assertIsNone(load_arrays(root)[0]['expected'])

    def test_integer_metadata_preserves_required_file_locations(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            metadata = self.make_task(root, 'a')
            document = yaml.safe_load(metadata.read_text())
            document['required_files'] = ['support.h']
            metadata.write_text(yaml.safe_dump(document))
            (root/'c/Arrays.set').write_text('a/*.yml\n')
            tasks = integer_tasks(load_arrays(root), root/'generated')
            generated = yaml.safe_load(Path(tasks[0]['metadata']).read_text())
            self.assertEqual(generated['required_files'], [str((metadata.parent/'support.h').resolve())])
            self.assertNotIn('expected_verdict', generated['properties'][0])

    def test_result_reader_keeps_family_qualified_identities(self):
        with TemporaryDirectory() as temporary:
            output = Path(temporary)
            tree = ET.Element('result')
            tasks = []
            for family in ('a', 'b'):
                metadata = output/family/'same.yml'
                tasks.append(dict(name=family+'__same', metadata=str(metadata)))
                run = ET.SubElement(tree, 'run', name=str(metadata))
                ET.SubElement(run, 'column', title='status', value='true')
            ET.ElementTree(tree).write(output/'arrays.results.tupling.xml')
            self.assertEqual([row['task'] for row in read_results(output, tasks)],
                             ['a__same', 'b__same'])


if __name__ == '__main__':
    unittest.main()
