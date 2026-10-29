import { Pipe, PipeTransform } from '@angular/core';

/** "3m ago" / "2h ago" formatting for every timestamp the backend returns
 * (registeredAt, lastSeenAt, receivedAt, issuedAt, attemptedAt, ...). */
@Pipe({ name: 'relativeTime', standalone: true, pure: false })
export class RelativeTimePipe implements PipeTransform {
  transform(value: string | Date | null | undefined): string {
    if (!value) return '—';
    const date = typeof value === 'string' ? new Date(value.endsWith('Z') ? value : value + 'Z') : value;
    const seconds = Math.round((Date.now() - date.getTime()) / 1000);
    if (Number.isNaN(seconds)) return '—';
    if (seconds < 5) return 'just now';
    if (seconds < 60) return `${seconds}s ago`;
    const minutes = Math.round(seconds / 60);
    if (minutes < 60) return `${minutes}m ago`;
    const hours = Math.round(minutes / 60);
    if (hours < 24) return `${hours}h ago`;
    const days = Math.round(hours / 24);
    if (days < 30) return `${days}d ago`;
    return date.toLocaleDateString();
  }
}
