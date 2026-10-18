"""Native Qt settings and embedded vendor client. No webview or browser runtime."""
import argparse
import json
import os
from pathlib import Path
import sys
os.environ.setdefault('QT_QPA_PLATFORM', 'xcb')
from PySide6.QtCore import QProcess, QTimer, Qt
from PySide6.QtGui import QIcon, QKeySequence, QShortcut, QWindow
from PySide6.QtNetwork import QLocalServer, QLocalSocket
from PySide6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QPushButton, QComboBox, QLineEdit, QScrollArea, QStackedWidget, QDialog,
    QDialogButtonBox, QFormLayout, QMessageBox, QPlainTextEdit, QFrame)
from . import __version__, config as c
from .hardware import inventory

STYLE = '''
QWidget { background:#2b2b2b; color:#e8e8e8; font-family:"Noto Sans"; font-size:13px; }
QLabel#title { font-size:34px; font-weight:500; }
QLabel#hint { color:#b9b9b9; font-size:11px; }
QLabel#section { font-size:15px; font-weight:700; }
QLabel#state { color:#3ca8d4; }
QFrame#sidebar { background:#282929; }
QPushButton { background:#242626; border:1px solid #202222; padding:9px 16px; border-radius:2px; }
QPushButton:hover { background:#35393b; }
QPushButton#link { color:#3ca8d4; background:transparent; border:0; padding:8px; }
QPushButton#primary { color:white; background:#208bb4; border:0; }
QPushButton#primary:disabled { color:#74797a; background:#292a2b; }
QPushButton#nav { color:white; background:transparent; border:0; font-size:26px; padding:12px; }
QPushButton#tab { color:#3ca8d4; background:transparent; border:0; padding:10px 14px 10px 0; }
QPushButton#tab:checked { font-weight:700; }
QPushButton:disabled { color:#74797a; background:#292a2b; }
QComboBox,QLineEdit { background:#242626; border:1px solid #202222; padding:9px; min-height:21px; }
QComboBox:disabled,QLineEdit:disabled { color:#777d80; }
QComboBox QAbstractItemView { background:#222426; selection-background-color:#323638; padding:6px; }
QScrollArea { border:0; }
QScrollBar:vertical { background:#2b2b2b; width:9px; margin:0; }
QScrollBar::handle:vertical { background:#55595b; min-height:32px; border-radius:3px; }
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical { height:0; }
QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical { background:transparent; }
QFrame[frameShape="4"] { color:#393a3a; max-height:1px; }
QPlainTextEdit { font-family:monospace; font-size:11px; background:#202222; }
'''


class Login(QDialog):
    def __init__(self, parent):
        super().__init__(parent)
        self.setWindowTitle('Connect Host account')
        self.setMinimumWidth(440)
        layout = QVBoxLayout(self)
        note = QLabel('Connect a separate Linux host session. Your existing Parsec client login is preserved.')
        note.setWordWrap(True)
        layout.addWidget(note)
        form = QFormLayout()
        self.email, self.password, self.tfa = QLineEdit(), QLineEdit(), QLineEdit()
        self.password.setEchoMode(QLineEdit.EchoMode.Password)
        for title, field in [('Email', self.email), ('Password', self.password), ('Two-factor code', self.tfa)]:
            form.addRow(title, field)
        layout.addLayout(form)
        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)


class Main(QMainWindow):
    def __init__(self, args):
        super().__init__()
        self.args, self.jobs, self.busy, self.loaded = args, set(), False, False
        self.foreign, self.xid, self.devices, self.session_state = None, None, [], None
        self.fields = {}
        self.setWindowTitle('Parsec · Linux Host')
        self.setWindowIcon(QIcon.fromTheme('parsecd'))
        self.resize(1380, 940)
        outer = QWidget()
        self.setCentralWidget(outer)
        row = QHBoxLayout(outer)
        row.setContentsMargins(0, 0, 0, 0)
        row.setSpacing(0)
        self.sidebar = QFrame()
        self.sidebar.setObjectName('sidebar')
        self.sidebar.setFixedWidth(64)
        nav = QVBoxLayout(self.sidebar)
        nav.setContentsMargins(0, 0, 0, 0)
        nav.addStretch()
        for text, tip, callback in [('▣', 'Computers', self.show_client), ('⚙', 'Settings', self.show_settings),
                                    ('?', 'About', self.about)]:
            button = QPushButton(text)
            button.setObjectName('nav')
            button.setToolTip(tip)
            button.clicked.connect(callback)
            nav.addWidget(button)
        nav.addStretch()
        row.addWidget(self.sidebar)
        self.pages = QStackedWidget()
        row.addWidget(self.pages, 1)
        self.settings_page = QWidget()
        layout = QVBoxLayout(self.settings_page)
        layout.setContentsMargins(38, 22, 38, 18)
        self.title = QLabel('Settings')
        self.title.setObjectName('title')
        layout.addWidget(self.title)
        subtitle = QLabel('Customize your Parsec experience.')
        subtitle.setObjectName('hint')
        layout.addWidget(subtitle)
        line = QFrame()
        line.setFrameShape(QFrame.Shape.HLine)
        layout.addWidget(line)
        tabs = QHBoxLayout()
        self.tab_buttons = []
        for index, title in enumerate(('Client', 'Host', 'Account')):
            button = QPushButton(title)
            button.setObjectName('tab')
            button.setCheckable(True)
            button.clicked.connect(lambda _, i=index: self.select_tab(i))
            self.tab_buttons.append(button)
            tabs.addWidget(button)
        tabs.addStretch()
        version = QLabel('Linux Host ' + __version__)
        version.setObjectName('hint')
        tabs.addWidget(version)
        layout.addLayout(tabs)
        self.tabs = QStackedWidget()
        layout.addWidget(self.tabs, 1)
        self.make_client()
        self.make_host()
        self.make_account()
        footer = QHBoxLayout()
        self.notice = QLabel('')
        self.notice.setObjectName('state')
        self.notice.setWordWrap(True)
        footer.addWidget(self.notice, 1)
        reset = QPushButton('Restore defaults')
        reset.clicked.connect(lambda: self.fill(c.DEFAULT))
        self.save = QPushButton('Save settings')
        self.save.setObjectName('primary')
        self.save.clicked.connect(self.save_settings)
        footer.addWidget(reset)
        footer.addWidget(self.save)
        layout.addLayout(footer)
        self.pages.addWidget(self.settings_page)
        self.client_page = QWidget()
        self.client_layout = QVBoxLayout(self.client_page)
        self.client_layout.setContentsMargins(0, 0, 0, 0)
        self.placeholder = QLabel('Open the Parsec client to view Computers and Friends.')
        self.placeholder.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.client_layout.addWidget(self.placeholder, 1)
        back = QPushButton('Settings → Host    Ctrl+Shift+H')
        back.clicked.connect(self.show_settings)
        self.client_layout.addWidget(back)
        self.pages.addWidget(self.client_page)
        self.select_tab(1)
        QShortcut(QKeySequence('Ctrl+Shift+H'), self, activated=self.show_settings)
        self.poll = QTimer(self)
        self.poll.timeout.connect(self.refresh)
        self.poll.start(4000)
        self.embed_timer = QTimer(self)
        self.embed_timer.timeout.connect(self.attach_client)
        if not args.preview:
            self.embed_timer.start(800)
        self.refresh()
        if args.snapshot:
            QTimer.singleShot(2000, lambda: self.grab().save(args.snapshot))
        if args.exit_after:
            QTimer.singleShot(args.exit_after, self.close)

    def panel(self, title):
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        content = QWidget()
        horizontal = QHBoxLayout(content)
        horizontal.setContentsMargins(20, 24, 20, 30)
        horizontal.addStretch()
        form = QWidget()
        form.setMaximumWidth(890)
        form.setMinimumWidth(740)
        layout = QVBoxLayout(form)
        layout.setSpacing(25)
        heading = QLabel(title)
        heading.setObjectName('section')
        layout.addWidget(heading)
        horizontal.addWidget(form, 3)
        horizontal.addStretch()
        scroll.setWidget(content)
        self.tabs.addWidget(scroll)
        return layout

    def setting(self, layout, title, description, control, key=None):
        row = QHBoxLayout()
        left = QVBoxLayout()
        left.setSpacing(6)
        left.addWidget(QLabel(title))
        detail = QLabel(description)
        detail.setObjectName('hint')
        detail.setWordWrap(True)
        detail.setMaximumWidth(440)
        left.addWidget(detail)
        row.addLayout(left, 1)
        row.addSpacing(28)
        control.setFixedWidth(350)
        row.addWidget(control, 0, Qt.AlignmentFlag.AlignTop)
        layout.addLayout(row)
        if key:
            self.fields[key] = control
        return control

    def combo(self, pairs):
        widget = QComboBox()
        for title, value in pairs:
            widget.addItem(title, value)
        return widget

    def toggle(self):
        return self.combo([('Disabled', False), ('Enabled', True)])

    def make_host(self):
        layout = self.panel('HOST SETTINGS')
        self.hosting = self.toggle()
        self.hosting.activated.connect(self.toggle_host)
        self.setting(layout, 'Hosting Enabled', 'Allow connections to this computer from your own account.', self.hosting)
        name = QLineEdit()
        name.setPlaceholderText('Use system hostname')
        self.setting(layout, 'Host Name', 'The name of your computer. Leave blank to use the hostname.', name, 'name')
        for title, value, description in [('Resolution', 'Keep current display mode', 'Uses the current Linux display mode.'),
                ('Orientation', 'Default', 'Uses the current display orientation.')]:
            field = self.combo([(value, None)])
            field.setEnabled(False)
            field.setToolTip('Automatic host display mode changes are not exposed in this release.')
            self.setting(layout, title, description, field)
        self.setting(layout, 'Bandwidth Limit', 'The maximum video bandwidth used by the host.',
            self.combo([(f'{n} Mbps', n) for n in (5, 7, 10, 15, 20, 25, 30, 35, 40, 50, 75, 100)]), 'bitrate')
        lock = self.toggle()
        lock.setEnabled(False)
        lock.setToolTip('Not implemented; the desktop is not automatically locked after disconnect.')
        self.setting(layout, 'Lock Desktop', 'Automatic locking after disconnect is unavailable in this release.', lock)
        self.setting(layout, 'Host GPU', 'Select the GPU connected to the display you want to stream.',
            self.combo([('Automatic · connected display', 'auto')]), 'gpu')
        self.fields['gpu'].currentIndexChanged.connect(self.update_outputs)
        self.setting(layout, 'Display', 'Select a connected monitor on the selected GPU.',
            self.combo([('Automatic', 'auto')]), 'output')
        self.setting(layout, 'Encoder', 'Automatic follows the capture GPU. CPU video encoding is unavailable.',
            self.combo([('Automatic', 'auto'), ('NVIDIA NVENC', 'nvenc'), ('AMD / Intel VA-API', 'vaapi')]), 'encoder')
        self.setting(layout, 'FPS', 'A frame rate limit; the display refresh rate remains the upper bound.',
            self.combo([(f'{n} FPS', n) for n in (30, 60, 90, 120, 144, 165, 240)]), 'fps')
        self.setting(layout, 'Video Codec', 'H.264 is the compatibility default. Both peers must support H.265.',
            self.combo([('H.264', 'h264'), ('H.265 / HEVC', 'h265')]), 'codec')
        self.setting(layout, 'Quality', 'Choose between response time and image quality.',
            self.combo([('Lowest Latency', 'lowest-latency'), ('Balanced', 'balanced'), ('Highest Quality', 'highest')]), 'quality')
        self.setting(layout, 'Capture Conversion', 'Vulkan supports NVENC. OpenGL is intended for VA-API paths.',
            self.combo([('Automatic', 'auto'), ('Vulkan', 'vulkan'), ('OpenGL / EGL', 'opengl')]), 'conversion')
        for key, title, description in [('full_fps', 'Constant FPS', 'Keep sending frames when the desktop is static.'),
            ('mute_speakers', 'Mute Speakers', 'Mute local speakers during an owner connection.'),
            ('clipboard', 'Copy and Paste', 'Share clipboard text with your own client.'),
            ('autostart', 'Start at Boot', 'Make this host available automatically when the computer starts.')]:
            self.setting(layout, title, description, self.toggle(), key)
        layout.addStretch()

    def make_client(self):
        layout = self.panel('CLIENT SETTINGS')
        self.setting(layout, 'Decoder', 'Hardware is faster. Software decoding runs on the CPU.',
            self.combo([('Hardware · GPU', 'hardware'), ('Software · CPU', 'software')]), 'client_decoder')
        self.setting(layout, 'Renderer', 'The rendering API used by the official Linux client.',
            self.combo([('OpenGL', 'opengl'), ('Vulkan · experimental', 'vulkan')]), 'client_renderer')
        open_client = QPushButton('Open Parsec client')
        open_client.clicked.connect(self.show_client)
        layout.addWidget(open_client, 0, Qt.AlignmentFlag.AlignLeft)
        note = QLabel('Client changes apply at the next client start. Close the running client before opening it again.')
        note.setObjectName('hint')
        note.setWordWrap(True)
        layout.addWidget(note)
        layout.addStretch()

    def make_account(self):
        layout = self.panel('HOST ACCOUNT')
        self.account = QLabel('Host account is not connected')
        layout.addWidget(self.account)
        button = QPushButton('Connect Host account')
        button.clicked.connect(self.login)
        layout.addWidget(button, 0, Qt.AlignmentFlag.AlignLeft)
        self.state = QLabel('')
        self.state.setObjectName('state')
        layout.addWidget(self.state)
        logs = QPushButton('Host logs')
        logs.clicked.connect(self.logs)
        layout.addWidget(logs, 0, Qt.AlignmentFlag.AlignLeft)
        layout.addStretch()

    def select_tab(self, index):
        self.tabs.setCurrentIndex(index)
        for i, button in enumerate(self.tab_buttons):
            button.setChecked(i == index)

    def show_settings(self):
        self.sidebar.show()
        self.pages.setCurrentIndex(0)
        self.select_tab(1)

    def show_client(self):
        if self.args.preview:
            self.notice.setText('Preview mode: the running client is left in its current window.')
            return
        self.sidebar.hide()
        self.pages.setCurrentIndex(1)
        if self.foreign is None:
            QProcess.startDetached(str(c.VENDOR), c.client_args(self.values()))
            self.embed_timer.start()

    def values(self):
        return c.validate({key: field.text() if isinstance(field, QLineEdit) else field.currentData()
                           for key, field in self.fields.items()})

    def save_settings(self):
        try:
            self.action('save', self.values())
        except ValueError as error:
            QMessageBox.warning(self, 'Settings', str(error))

    def fill(self, settings):
        for key, field in self.fields.items():
            if isinstance(field, QLineEdit):
                field.setText(settings[key])
            else:
                index = field.findData(settings[key])
                if index >= 0:
                    field.setCurrentIndex(index)

    def update_outputs(self):
        field = self.fields.get('output')
        if field is None:
            return
        selected = field.currentData()
        field.clear()
        field.addItem('Automatic', 'auto')
        pci = self.fields['gpu'].currentData()
        seen = set()
        for dev in self.devices:
            if pci not in ('auto', dev['pci']):
                continue
            for connector in dev['outputs']:
                if connector not in seen:
                    field.addItem(connector, connector)
                    seen.add(connector)
        index = field.findData(selected)
        field.setCurrentIndex(max(0, index))

    def call(self, action, data, privileged, callback):
        p = QProcess(self)
        self.jobs.add(p)
        completed = False
        def finish(*_):
            nonlocal completed
            if completed:
                return
            completed = True
            try:
                answer = json.loads(bytes(p.readAllStandardOutput()))
            except ValueError:
                answer = {'ok': False, 'error': 'Authentication cancelled or component not installed'}
            self.jobs.discard(p)
            p.deleteLater()
            callback(answer)
        p.finished.connect(finish)
        p.errorOccurred.connect(lambda err: finish() if err == QProcess.ProcessError.FailedToStart else None)
        p.setProgram('/usr/bin/pkexec' if privileged else c.HELPER)
        p.setArguments([c.HELPER, action] if privileged else [action])
        p.start()
        p.write(json.dumps(data).encode())
        p.closeWriteChannel()

    def action(self, action, data=None, after=None):
        if self.args.preview or self.busy:
            return
        try:
            if action == 'save':
                data = c.validate(data)
        except ValueError as error:
            QMessageBox.warning(self, 'Settings', str(error))
            return
        self.busy = True
        self.notice.setText('Working… complete system authentication if requested.')
        def done(answer):
            self.busy = False
            if not answer.get('ok'):
                message = answer.get('error', 'Operation failed')
                self.notice.setText(message)
                QMessageBox.warning(self, 'Linux Host', message)
            else:
                result = answer.get('result', {})
                self.notice.setText('Saved. Restart hosting to apply changes.' if result.get('restart_required') else 'Done.')
                if after:
                    after(result)
            self.refresh()
        self.call(action, data or {}, True, done)

    def refresh(self):
        if self.args.preview:
            self.on_status({'ok': True, 'result': dict(installed=False, configured=False, account='',
                service={'ActiveState': 'inactive'}, settings=c.DEFAULT, devices=inventory())})
        elif not self.jobs:
            self.call('status', {}, False, self.on_status)

    def on_status(self, answer):
        if not answer.get('ok'):
            self.notice.setText('Install the package to enable hosting controls.')
            return
        data = answer['result']
        active = data['service'].get('ActiveState') == 'active'
        self.hosting.setCurrentIndex(int(active))
        self.hosting.setEnabled(data['installed'] and not self.busy)
        self.save.setEnabled(data['installed'] and not self.args.preview and not self.busy)
        self.state.setText('Host service running' if active else 'Host service stopped')
        self.account.setText('Host account: ' + data['account'] if data['configured'] else 'Connect your Host account to enable hosting.')
        self.configured = data['configured']
        if not self.loaded:
            self.devices = data['devices']
            for device in self.devices:
                self.fields['gpu'].addItem(device['label'], device['pci'])
            self.update_outputs()
            self.fill(data['settings'])
            self.loaded = True
        if self.args.preview:
            self.notice.setText('Development preview · no settings or running services are changed.')
        elif self.session_state != active:
            self.session_state = active
            QProcess.startDetached('/usr/bin/systemctl', ['--user', 'start' if active else 'stop', c.USER_UNIT])

    def toggle_host(self):
        if self.hosting.currentData():
            if not self.configured:
                self.hosting.setCurrentIndex(0)
                self.select_tab(2)
                self.login()
            else:
                try:
                    settings = self.values()
                except ValueError as error:
                    QMessageBox.warning(self, 'Settings', str(error))
                    return
                self.action('save', settings, after=lambda _: self.action('start'))
        else:
            self.action('stop')

    def login(self):
        if self.args.preview:
            return
        dialog = Login(self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            data = dict(email=dialog.email.text(), password=dialog.password.text(), tfa=dialog.tfa.text())
            dialog.password.clear()
            self.action('login', data)
        dialog.deleteLater()

    def logs(self):
        def show(result):
            dialog = QDialog(self)
            dialog.setWindowTitle('Host logs')
            dialog.resize(900, 520)
            layout = QVBoxLayout(dialog)
            text = QPlainTextEdit(result.get('logs', ''))
            text.setReadOnly(True)
            layout.addWidget(text)
            dialog.exec()
        self.action('logs', after=show)

    def about(self):
        QMessageBox.information(self, 'About', 'Parsec Linux Host ' + __version__ +
            '\nIndependent Qt integration and lowlat hosting backend.\n'
            'The proprietary Parsec client remains unmodified.\nNot affiliated with Parsec or Unity.')

    def attach_client(self):
        if self.foreign is not None:
            return
        try:
            from Xlib import X, display
            d = display.Display()
            root = d.screen().root
            prop = root.get_full_property(d.intern_atom('_NET_CLIENT_LIST'), X.AnyPropertyType)
            for xid in prop.value if prop is not None else []:
                win = d.create_resource_object('window', int(xid))
                classes = [str(i).lower() for i in win.get_wm_class() or ()]
                if 'parsecd' not in classes:
                    continue
                self.xid = int(xid)
                self.geometry_before = win.get_geometry()
                self.foreign = QWindow.fromWinId(self.xid)
                self.container = QWidget.createWindowContainer(self.foreign, self.client_page)
                self.client_layout.replaceWidget(self.placeholder, self.container)
                self.placeholder.hide()
                self.embed_timer.stop()
                break
            d.close()
        except Exception:
            pass

    def closeEvent(self, event):
        self.embed_timer.stop()
        if self.foreign is not None:
            self.foreign.setParent(None)
            try:
                from Xlib import display
                d = display.Display()
                win = d.create_resource_object('window', self.xid)
                win.reparent(d.screen().root, 100, 100)
                win.configure(width=max(960, self.geometry_before.width), height=max(640, self.geometry_before.height))
                win.map()
                d.sync()
                d.close()
            except Exception:
                pass
        super().closeEvent(event)
        QTimer.singleShot(0, QApplication.instance().quit)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--preview', action='store_true', help='Read-only source-tree preview')
    parser.add_argument('--snapshot')
    parser.add_argument('--exit-after', type=int, default=0)
    args = parser.parse_args()
    app = QApplication(sys.argv[:1])
    app.setApplicationName('Parsec Linux Host')
    app.setDesktopFileName('parsec-linux-host-integrated')
    app.setStyleSheet(STYLE)
    name = 'parsec-linux-host-' + str(os.getuid()) + ('-preview' if args.preview else '')
    socket = QLocalSocket()
    socket.connectToServer(name)
    if socket.waitForConnected(150):
        socket.write(b'show')
        socket.waitForBytesWritten(150)
        return 0
    server = QLocalServer()
    server.setSocketOptions(QLocalServer.SocketOption.UserAccessOption)
    server.removeServer(name)
    server.listen(name)
    window = Main(args)
    def show():
        connection = server.nextPendingConnection()
        connection.close()
        window.showNormal()
        window.raise_()
        window.activateWindow()
        window.show_settings()
    server.newConnection.connect(show)
    window.show()
    return app.exec()
