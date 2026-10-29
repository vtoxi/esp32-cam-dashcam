import { Component, Input, computed, signal } from '@angular/core';

export type BadgeTone = 'success' | 'warning' | 'danger' | 'neutral' | 'info';

const TONE_BY_KEYWORD: Record<string, BadgeTone> = {
  online: 'success',
  executed: 'success',
  delivered: 'info',
  success: 'success',
  true: 'success',
  offline: 'neutral',
  pending: 'warning',
  confirmed: 'warning',
  retry_backoff: 'warning',
  failed: 'danger',
  false: 'danger',
  expired: 'danger',
  rejected: 'danger',
  auth_failed: 'danger',
  disarmed: 'neutral',
  driving: 'info',
  parked: 'success',
  service: 'warning',
  local_only: 'neutral',
  connected: 'success',
  connecting: 'warning',
};

const TONE_CLASSES: Record<BadgeTone, string> = {
  success: 'bg-emerald-400/15 text-emerald-400 ring-1 ring-inset ring-emerald-400/25',
  warning: 'bg-amber-400/15 text-amber-400 ring-1 ring-inset ring-amber-400/25',
  danger: 'bg-rose-400/15 text-rose-400 ring-1 ring-inset ring-rose-400/25',
  info: 'bg-sky-400/15 text-sky-400 ring-1 ring-inset ring-sky-400/25',
  neutral: 'bg-slate-500/15 text-slate-400 ring-1 ring-inset ring-slate-500/25',
};

/** One lookup table so every status-like value (device online/offline, command
 * status, incident state, webhook delivery success) gets consistent coloring
 * across the whole app instead of each page picking colors ad hoc. */
@Component({
  selector: 'app-status-badge',
  standalone: true,
  template: `
    <span class="inline-flex items-center px-2.5 py-0.5 rounded-full text-[11px] font-semibold uppercase tracking-wide {{ classes() }}">
      @if (dot) {
        <span class="w-1.5 h-1.5 rounded-full mr-1.5 -ml-0.5" [class]="dotClasses()"></span>
      }
      {{ label() }}
    </span>
  `,
})
export class StatusBadgeComponent {
  private readonly valueSignal = signal<string | boolean | null | undefined>(undefined);
  private readonly toneOverride = signal<BadgeTone | undefined>(undefined);

  @Input() dot = false;
  @Input() set value(v: string | boolean | null | undefined) {
    this.valueSignal.set(v);
  }
  @Input('tone') set toneInput(t: BadgeTone | undefined) {
    this.toneOverride.set(t);
  }

  readonly label = computed(() => {
    const v = this.valueSignal();
    if (v === null || v === undefined || v === '') return '—';
    return String(v).replace(/_/g, ' ');
  });

  readonly tone = computed<BadgeTone>(() => {
    if (this.toneOverride()) return this.toneOverride()!;
    const key = String(this.valueSignal() ?? '').toLowerCase();
    return TONE_BY_KEYWORD[key] ?? 'neutral';
  });

  readonly classes = computed(() => TONE_CLASSES[this.tone()]);
  readonly dotClasses = computed(() => {
    const t = this.tone();
    return {
      success: 'bg-emerald-400',
      warning: 'bg-amber-400',
      danger: 'bg-rose-400',
      info: 'bg-sky-400',
      neutral: 'bg-slate-400',
    }[t];
  });
}
