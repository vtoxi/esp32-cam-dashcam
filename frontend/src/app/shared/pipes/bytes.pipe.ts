import { Pipe, PipeTransform } from '@angular/core';

/** Human-readable file size for EvidenceRecord.sizeBytes. */
@Pipe({ name: 'bytes', standalone: true })
export class BytesPipe implements PipeTransform {
  transform(value: number | null | undefined): string {
    if (value === null || value === undefined) return '—';
    if (value < 1024) return `${value} B`;
    const units = ['KB', 'MB', 'GB'];
    let size = value / 1024;
    let unitIndex = 0;
    while (size >= 1024 && unitIndex < units.length - 1) {
      size /= 1024;
      unitIndex++;
    }
    return `${size.toFixed(size < 10 ? 1 : 0)} ${units[unitIndex]}`;
  }
}
