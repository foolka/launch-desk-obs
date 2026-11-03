const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { run } = require('./build.cjs');
const root = path.resolve(__dirname, '..');
const version = require('../package.json').version;
const release = path.join(root, 'release');
const stage = path.join(release, 'payload-' + version);
const dll = path.join(stage, 'launch-desk-obs/bin/64bit');
fs.mkdirSync(dll, { recursive: true });
fs.mkdirSync(path.join(stage, 'launch-desk-obs/data'), { recursive: true });
fs.copyFileSync(path.join(root, 'build/Release/launch-desk-obs.dll'), path.join(dll, 'launch-desk-obs.dll'));
for (const file of ['README.md', 'README.ru.md', 'README.uk.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'CHANGELOG.md']) fs.copyFileSync(path.join(root, file), path.join(stage, file));
fs.writeFileSync(path.join(stage, 'launch-desk-obs/data/version.json'), JSON.stringify({ version, product: 'launch-desk-obs' }, null, 2) + '\n');
const quote = value => "'" + value.replaceAll("'", "''") + "'";
const base = `launch-desk-obs-${version}-windows-x64`;
const archive = path.join(release, base + '.zip');
run('powershell.exe', ['-NoProfile', '-Command', `Compress-Archive -Path ${quote(stage + '/*')} -DestinationPath ${quote(archive)} -Force`]);
const iscc = process.env.ISCC_EXE || 'C:/Program Files (x86)/Inno Setup 6/ISCC.exe';
run(iscc, ['/Qp', '/DAppVersion=' + version, '/DPackageDir=' + stage, '/DOutputDir=' + release, path.join(root, 'tools/installer.iss')]);
for (const name of [base + '.zip', base + '-setup.exe']) {
	const hash = crypto.createHash('sha256').update(fs.readFileSync(path.join(release, name))).digest('hex');
	fs.writeFileSync(path.join(release, name + '.sha256'), `${hash}  ${name}\n`);
	console.log(name + ' ' + hash);
}
