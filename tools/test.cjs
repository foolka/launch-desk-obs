const path = require('node:path');
const { run, environment } = require('./build.cjs');
const root = path.resolve(__dirname, '..');
const qt = process.env.QT_SDK || path.join(root, '.deps/qt6');
const env = environment();
env.Path = path.join(qt, 'bin') + ';' + env.Path;
env.QT_PLUGIN_PATH = path.join(qt, 'plugins');
env.QT_QPA_PLATFORM = 'minimal';
run(path.join(root, 'build/Release/launch-tests.exe'), [], { env });
run(path.join(root, 'build/Release/launch-host-tests.exe'), ['--launch-desk-test'], { env });
env.QT_QPA_PLATFORM = 'windows';
for (const locale of ['ru', 'uk', 'en']) {
	run(path.join(root, 'build/Release/launch-preview.exe'), [locale, path.join(root, 'docs/screenshots', locale + '.png')], { env });
}
