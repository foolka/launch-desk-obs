const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');
const root = path.resolve(__dirname, '..');

// Normalize Windows environment keys before invoking MSBuild.
function environment() {
	const env = {};
	for (const [key, value] of Object.entries(process.env)) env[key.toUpperCase() === 'PATH' ? 'Path' : key.toUpperCase()] = value;
	env.MSBUILDDISABLENODEREUSE = '1';
	return env;
}
function run(file, args, options = {}) {
	const result = spawnSync(file, args, { cwd: root, env: environment(), stdio: 'inherit', windowsHide: true, ...options });
	if (result.error || result.status !== 0) throw Error(`Command failed: ${path.basename(file)}`);
	return result;
}
async function build() {
	const locator = path.join(process.env['ProgramFiles(x86)'] || 'C:/Program Files (x86)', 'Microsoft Visual Studio/Installer/vswhere.exe');
	const result = run(locator, ['-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-format', 'json'], { stdio: 'pipe', encoding: 'utf8' });
	const vs = JSON.parse(result.stdout)[0];
	if (!vs) throw Error('Visual Studio 2022 C++ Build Tools are required');
	const cmake = process.env.CMAKE_EXE || path.join(vs.installationPath, 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe');
	const qt = process.env.QT_SDK || path.join(root, '.deps/qt6');
	if (!fs.existsSync(path.join(qt, 'lib/cmake/Qt6/Qt6Config.cmake'))) {
		const response = await fetch('https://github.com/obsproject/obs-deps/releases/download/2025-07-11/windows-deps-qt6-2025-07-11-x64-RelWithDebInfo.zip', { signal: AbortSignal.timeout(300000) });
		if (!response.ok) throw Error('Official OBS Qt SDK download failed');
		const bytes = Buffer.from(await response.arrayBuffer());
		if (crypto.createHash('sha256').update(bytes).digest('hex') !== 'b68f98f8bb5b81d446953ff1c35bbbc8d98dbce0e0d787664dce84ec88749648') throw Error('Qt SDK checksum mismatch');
		fs.mkdirSync(path.join(root, '.deps'), { recursive: true });
		const zip = path.join(root, '.deps/qt6.zip');
		fs.writeFileSync(zip, bytes);
		const quote = value => "'" + value.replaceAll("'", "''") + "'";
		run('powershell.exe', ['-NoProfile', '-Command', `Expand-Archive -LiteralPath ${quote(zip)} -DestinationPath ${quote(qt)} -Force`]);
	}
	const generator = Number(vs.installationVersion.split('.')[0]) >= 17 ? 'Visual Studio 17 2022' : 'Visual Studio 16 2019';
	run(cmake, ['-S', root, '-B', 'build', '-G', generator, '-A', 'x64', `-DCMAKE_PREFIX_PATH=${qt}`]);
	run(cmake, ['--build', 'build', '--config', 'Release', '--parallel', '1', '--', '/nodeReuse:false']);
}
if (require.main === module) build().catch(error => { console.error(error.message); process.exitCode = 1; });
module.exports = { run, environment };
