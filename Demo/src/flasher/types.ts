interface ManifestPart {
    path: string;
    offset: number | string;
}

interface ManifestBuild {
    parts: ManifestPart[];
}

interface Manifest {
    builds: ManifestBuild[];
}
