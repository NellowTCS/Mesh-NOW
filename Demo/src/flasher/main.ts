import SparkMD5 from 'spark-md5';
import { ESPLoader, Transport } from 'esptool-js';

const NUMBER_OF_VERSIONS_SHOWN = 5;

const versionSelect = document.getElementById(
    'version-select'
) as HTMLSelectElement;
const latestVersionText = document.getElementById(
    'latest-version-text'
) as HTMLDivElement;
const targetSelect = document.getElementById(
    'target-select'
) as HTMLSelectElement;
const flashBtn = document.getElementById('flash-btn') as HTMLButtonElement;
const statusDiv = document.getElementById('status') as HTMLDivElement;
const preserveDataCheckbox = document.getElementById(
    'preserve-data'
) as HTMLInputElement;
const progressBar = document.getElementById('progress-bar') as HTMLDivElement;
const progressFill = document.getElementById('progress-fill') as HTMLDivElement;
const progressText = document.getElementById('progress-text') as HTMLDivElement;
const devInfo = document.getElementById('dev-info') as HTMLDivElement;

let firmwareFiles: {
    bootloader: ManifestPart | null;
    partitions: ManifestPart | null;
    application: ManifestPart | null;
} = { bootloader: null, partitions: null, application: null };
let currentFirmwareBaseUrl = '';
let esploader: ESPLoader | null = null;
let transport: Transport | null = null;

const espLoaderTerminal = {
    clean(): void {
        console.clear();
    },
    writeLine(data: string): void {
        console.log(data);
    },
    write(data: string): void {
        console.log(data);
    },
};

function parseOffset(off: number | string): number {
    if (typeof off === 'number' && !isNaN(off)) return off;
    if (typeof off === 'string') {
        const s = off.trim();
        if (s.length === 0) return 0;
        if (s.startsWith('0x') || s.startsWith('0X')) {
            const v = parseInt(s, 16);
            return isNaN(v) ? 0 : v;
        }
        const v = parseInt(s, 10);
        return isNaN(v) ? 0 : v;
    }
    return 0;
}

function formatDevBuild(version: string): string | null {
    if (!version || typeof version !== 'string') return null;
    const m = version.match(/^dev-(\d{8})-(\d{6})$/);
    if (!m) return null;
    const datePart = m[1] ?? '';
    const timePart = m[2] ?? '';
    const year = parseInt(datePart.slice(0, 4), 10);
    const month = parseInt(datePart.slice(4, 6), 10) - 1;
    const day = parseInt(datePart.slice(6, 8), 10);
    const hours = parseInt(timePart.slice(0, 2), 10);
    const minutes = parseInt(timePart.slice(2, 4), 10);
    const seconds = parseInt(timePart.slice(4, 6), 10);

    const ts = Date.UTC(year, month, day, hours, minutes, seconds);
    const d = new Date(ts);
    const pad = (n: number) => String(n).padStart(2, '0');
    const formatted =
        `${d.getUTCFullYear()}-${pad(d.getUTCMonth() + 1)}-${pad(d.getUTCDate())} ` +
        `${pad(d.getUTCHours())}:${pad(d.getUTCMinutes())}:${pad(d.getUTCSeconds())} UTC`;
    return formatted;
}

async function loadVersions(): Promise<void> {
    try {
        const response = await fetch('./manifest-index.json', {
            cache: 'no-store',
        });
        if (!response.ok)
            throw new Error(
                `manifest-index.json fetch failed: ${response.status}`
            );
        const data = (await response.json()) as { versions: string[] };

        versionSelect.innerHTML = '';

        if (!data.versions || data.versions.length === 0) {
            versionSelect.innerHTML =
                '<option value="">No versions available</option>';
            statusDiv.textContent = 'No firmware versions found';
            return;
        }

        const devVersions = data.versions
            .filter((v) => v.startsWith('dev-'))
            .sort((a, b) => b.localeCompare(a));
        const releaseVersions = data.versions
            .filter((v) => !v.startsWith('dev-'))
            .sort((a, b) => {
                const numsA = (a.match(/\d+/g) ?? []).map(Number);
                const numsB = (b.match(/\d+/g) ?? []).map(Number);
                for (let i = 0; i < Math.max(numsA.length, numsB.length); i++) {
                    const numA = numsA[i] || 0;
                    const numB = numsB[i] || 0;
                    if (numA !== numB) {
                        return numB - numA;
                    }
                }
                return b.localeCompare(a);
            });

        const releasesToShow = releaseVersions.slice(
            0,
            NUMBER_OF_VERSIONS_SHOWN
        );

        if (releasesToShow.length === 0 && devVersions.length === 0) {
            versionSelect.innerHTML =
                '<option value="">No versions available</option>';
            statusDiv.textContent = 'No firmware versions found';
            return;
        }

        const firstRelease = releasesToShow[0];
        if (firstRelease)
            latestVersionText.textContent = `The latest firmware is ${firstRelease}`;

        releasesToShow.forEach((version) => {
            const option = document.createElement('option');
            option.value = version;
            option.textContent = version;
            versionSelect.appendChild(option);
        });

        devVersions.forEach((version) => {
            const option = document.createElement('option');
            option.value = version;
            option.textContent = version;
            versionSelect.appendChild(option);
        });

        versionSelect.value = firstRelease ?? devVersions[0] ?? '';
        await updateStatus();
    } catch (error) {
        console.error('Failed to load versions:', error);
        versionSelect.innerHTML =
            '<option value="">Error loading versions</option>';
        statusDiv.textContent = 'Failed to load version list';
    }
}

async function updateStatus(): Promise<void> {
    const version = versionSelect.value;
    const target = targetSelect.value;
    const preserveData = preserveDataCheckbox.checked;

    devInfo.style.display = 'none';
    devInfo.textContent = '';
    currentFirmwareBaseUrl = '';

    if (!version) {
        flashBtn.disabled = true;
        flashBtn.textContent = 'Select firmware version';
        statusDiv.textContent = 'Please select a version';
        return;
    }

    try {
        let manifestUrl = `./firmware/${version}/${target}/manifest.json`;
        let manifestResponse = await fetch(manifestUrl, { cache: 'no-store' });
        let targetFolder = target;

        if (!manifestResponse.ok) {
            manifestUrl = `./firmware/${version}/manifest.json`;
            manifestResponse = await fetch(manifestUrl, { cache: 'no-store' });
            if (!manifestResponse.ok) {
                throw new Error(
                    `Failed to fetch manifest: ${manifestResponse.status}`
                );
            }
            targetFolder = '';
        }

        currentFirmwareBaseUrl = targetFolder
            ? `./firmware/${version}/${targetFolder}`
            : `./firmware/${version}`;

        const manifest = (await manifestResponse.json()) as Manifest;

        if (
            !manifest.builds ||
            !manifest.builds[0] ||
            !Array.isArray(manifest.builds[0].parts)
        ) {
            throw new Error(
                'Invalid manifest structure: builds[0].parts missing'
            );
        }

        const build = manifest.builds[0];
        const findPart = (pred: (p: ManifestPart) => boolean) =>
            build.parts.find(pred) ?? null;

        const bootloaderPart = findPart(
            (p) => !!(p.path && p.path.toLowerCase().includes('bootloader'))
        );
        const partitionsPart = findPart(
            (p) => !!(p.path && p.path.toLowerCase().includes('partition'))
        );
        const applicationPart = findPart(
            (p) =>
                !!(
                    p.path &&
                    !p.path.toLowerCase().includes('bootloader') &&
                    !p.path.toLowerCase().includes('partition')
                )
        );

        firmwareFiles = {
            bootloader: bootloaderPart
                ? {
                      path: bootloaderPart.path,
                      offset: parseOffset(bootloaderPart.offset),
                  }
                : null,
            partitions: partitionsPart
                ? {
                      path: partitionsPart.path,
                      offset: parseOffset(partitionsPart.offset),
                  }
                : null,
            application: applicationPart
                ? {
                      path: applicationPart.path,
                      offset: parseOffset(applicationPart.offset),
                  }
                : null,
        };

        flashBtn.disabled = false;
        flashBtn.textContent = `Flash ${version}`;
        statusDiv.textContent = preserveData
            ? `Ready to flash ${version} - Update (preserve data)`
            : `Ready to flash ${version} - Full installation`;

        const devFormatted = formatDevBuild(version);
        if (devFormatted) {
            devInfo.style.display = 'block';
            devInfo.title = devFormatted;
            devInfo.textContent = `Dev Build: ${devFormatted}`;
        }
    } catch (error) {
        console.error('Manifest load error:', error);
        flashBtn.disabled = true;
        const message = error instanceof Error ? error.message : String(error);
        statusDiv.innerHTML = `Error loading manifest:<br><span style="font-size:16px;color:#d32f2f;font-weight:bold;">${message}</span>`;
    }
}

async function initializeEspLoader(): Promise<boolean> {
    try {
        const port = await navigator.serial.requestPort();
        transport = new Transport(port, true);

        const flashOptions = {
            transport,
            baudrate: 115200,
            terminal: espLoaderTerminal,
        };
        esploader = new ESPLoader(flashOptions);

        const chip = await esploader.main();
        console.debug('Detected chip:', chip);
        if (!chip) {
            throw new Error('Failed to detect chip type');
        }

        return true;
    } catch (error) {
        console.error('Failed to initialize ESP loader:', error);
        statusDiv.textContent = `Error: ${error instanceof Error ? error.message : String(error)}`;
        return false;
    }
}

async function fetchUint8(path: string): Promise<Uint8Array> {
    const r = await fetch(path);
    if (!r.ok) throw new Error(`Failed to fetch ${path}: ${r.status}`);
    const buf = await r.arrayBuffer();
    return new Uint8Array(buf);
}

function binaryFileMd5(u8: Uint8Array): string {
    const copy = new Uint8Array(u8.byteLength);
    copy.set(u8);
    return SparkMD5.ArrayBuffer.hash(copy.buffer);
}

async function flashFirmware(): Promise<void> {
    const version = versionSelect.value;
    const preserveData = preserveDataCheckbox.checked;

    if (!version || !currentFirmwareBaseUrl) {
        statusDiv.textContent = 'Please select a valid version';
        return;
    }

    try {
        flashBtn.disabled = true;
        progressBar.style.display = 'block';
        statusDiv.textContent = 'Connecting to device...';

        const initialized = await initializeEspLoader();
        if (!initialized || !esploader) {
            return;
        }

        if (!firmwareFiles.application) {
            throw new Error(
                'Manifest is missing application/firmware entry (cannot locate firmware file path). Check manifest.json.'
            );
        }

        statusDiv.textContent = 'Loading firmware files...';

        const preparedFiles: { data: Uint8Array; address: number }[] = [];

        if (!preserveData) {
            if (firmwareFiles.bootloader && firmwareFiles.bootloader.path) {
                const bootloaderU8 = await fetchUint8(
                    `${currentFirmwareBaseUrl}/${firmwareFiles.bootloader.path}`
                );
                preparedFiles.push({
                    data: bootloaderU8,
                    address: Number(firmwareFiles.bootloader.offset),
                });
            } else {
                console.warn(
                    'Bootloader entry missing in manifest; skipping bootloader.'
                );
            }

            if (firmwareFiles.partitions && firmwareFiles.partitions.path) {
                const partitionsU8 = await fetchUint8(
                    `${currentFirmwareBaseUrl}/${firmwareFiles.partitions.path}`
                );
                preparedFiles.push({
                    data: partitionsU8,
                    address: Number(firmwareFiles.partitions.offset),
                });
            } else {
                console.warn(
                    'Partitions entry missing in manifest; skipping partitions.'
                );
            }
        }

        const appU8 = await fetchUint8(
            `${currentFirmwareBaseUrl}/${firmwareFiles.application.path}`
        );
        preparedFiles.push({
            data: appU8,
            address: Number(firmwareFiles.application.offset),
        });

        console.debug(
            'Prepared files for flashing:',
            preparedFiles.map((f) => ({
                address: '0x' + Number(f.address).toString(16),
                length: f.data.length,
            }))
        );

        statusDiv.textContent = 'Flashing firmware...';

        const flashOptions = {
            fileArray: preparedFiles.map((f) => ({
                data: f.data,
                address: Number(f.address),
            })),
            flashMode: 'keep' as const,
            flashFreq: 'keep' as const,
            flashSize: 'keep' as const,
            eraseAll: !preserveData,
            compress: true,
            reportProgress: (
                fileIndex: number,
                written: number,
                total: number
            ) => {
                const percent = Math.round((written / total) * 100);
                progressFill.style.width = `${percent}%`;
                progressText.textContent = `${percent}%`;
            },
            calculateMD5Hash: (image: Uint8Array) => binaryFileMd5(image),
        };

        await esploader.writeFlash(flashOptions);

        await esploader.after('hard_reset');

        statusDiv.textContent =
            'Firmware flashed successfully! Device is rebooting...';
        progressFill.style.width = '100%';
        progressText.textContent = '100%';
    } catch (error) {
        console.error('Flashing failed:', error);
        statusDiv.textContent = `Flashing failed: ${error instanceof Error ? error.message : String(error)}`;
    } finally {
        flashBtn.disabled = false;
        setTimeout(() => {
            progressBar.style.display = 'none';
            progressFill.style.width = '0%';
            progressText.textContent = '0%';
        }, 3000);
    }
}

versionSelect.addEventListener('change', updateStatus);
targetSelect.addEventListener('change', updateStatus);
preserveDataCheckbox.addEventListener('change', updateStatus);
flashBtn.addEventListener('click', flashFirmware);

window.addEventListener('load', () => {
    loadVersions();
});
