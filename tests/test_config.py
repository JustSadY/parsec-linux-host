import unittest
from parsec_host import config as c
from parsec_host.hardware import resolve

NVIDIA = dict(card='card7', pci='0000:01:00.0', vendor='10de', outputs=['DP-1'])
AMD = dict(card='card2', pci='0000:05:00.0', vendor='1002', outputs=[])


class Settings(unittest.TestCase):
    def test_documented_defaults(self):
        self.assertEqual((c.DEFAULT['fps'], c.DEFAULT['bitrate'], c.DEFAULT['client_renderer']), (60, 10, 'opengl'))
        self.assertEqual(c.DEFAULT['name'], '')

    def test_reject_config_as_code_or_options(self):
        for bad in (dict(name='--accept-microphone'), dict(name='bad\nname'), dict(gpu='../../dev'),
                    dict(fps=True), dict(fps=999), dict(bitrate=-1), dict(output=';sh'),
                    dict(encoder='cpu'), dict(command='sh'), dict(autostart=1),
                    dict(client_renderer='d3d11'), dict(encoder='nvenc', conversion='opengl')):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                c.validate(bad)

    def test_auto_uses_connected_gpu_not_kernel_number(self):
        self.assertEqual(resolve(c.DEFAULT, [AMD, NVIDIA])['card'], 'card7')
        args = c.host_args(c.DEFAULT, NVIDIA)
        self.assertIn('card7:DP-1', args)
        self.assertIn('--owner-only', args)
        self.assertNotIn('--encoder', args)
        self.assertNotIn('--no-host-mute', args)

    def test_pci_selection_requires_connected_monitor(self):
        with self.assertRaises(ValueError):
            resolve(c.validate({'gpu': AMD['pci']}), [AMD, NVIDIA])

    def test_encoder_device_validation(self):
        with self.assertRaises(ValueError):
            c.host_args(c.validate({'encoder': 'vaapi'}), NVIDIA)
        with self.assertRaises(ValueError):
            c.host_args(c.validate({'encoder': 'nvenc'}), AMD)
        args = c.host_args(c.validate({'encoder': 'nvenc', 'conversion': 'vulkan', 'full_fps': True}), NVIDIA)
        self.assertEqual(args[args.index('--encoder') + 1], 'nvenc')
        self.assertIn('--full-fps', args)

    def test_client_cpu_option_is_decoding_only(self):
        self.assertEqual(c.client_args(c.validate({'client_decoder': 'software', 'client_renderer': 'vulkan'})),
                         ['decoder_software=1:client_renderer=5'])
        self.assertEqual(c.DEFAULT['encoder'], 'auto')
