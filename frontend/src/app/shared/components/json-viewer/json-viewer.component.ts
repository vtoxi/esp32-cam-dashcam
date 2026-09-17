import { Component, Input, computed, signal } from '@angular/core';
import { IconComponent } from '../icon/icon.component';

/**
 * Every payload the backend passes through as an opaque JSON string
 * (TelemetryRecord.payloadJson, EventRecord.payloadJson,
 * IncidentRecord.payloadJson, CommandRecord.payloadJson/resultJson, and the
 * SSE ActivityItem.payload) lands here — one collapsible pretty-printed
 * viewer instead of each page re-implementing JSON.stringify+<pre>.
 */
@Component({
  selector: 'app-json-viewer',
  standalone: true,
  imports: [IconComponent],
  template: `
    @if (formatted(); as text) {
      <div class="flex items-start gap-1 max-w-[420px]">
        <button
          type="button"
          class="focus-ring flex items-center justify-center w-6 h-6 rounded text-slate-500 hover:text-white hover:bg-white/10 shrink-0 mt-0.5"
          (click)="expanded.set(!expanded())"
          [attr.aria-label]="expanded() ? 'Collapse' : 'Expand'"
        >
          <app-icon [name]="expanded() ? 'chevron-up' : 'chevron-down'" class="w-3.5 h-3.5" />
        </button>
        @if (expanded()) {
          <pre class="m-0 px-3 py-2.5 rounded-lg bg-surface-200/70 border border-white/5 text-[11px] leading-relaxed font-mono whitespace-pre-wrap break-words max-h-80 overflow-auto text-slate-300">{{ text }}</pre>
        } @else {
          <code class="inline-block pt-1 text-xs text-slate-500 whitespace-nowrap overflow-hidden text-ellipsis max-w-[380px] font-mono">{{ preview() }}</code>
        }
      </div>
    } @else {
      <span class="text-slate-600">—</span>
    }
  `,
})
export class JsonViewerComponent {
  private readonly raw = signal<string | null | undefined>(undefined);
  readonly expanded = signal(false);

  @Input() set json(value: string | null | undefined) {
    this.raw.set(value);
  }
  @Input() set data(value: unknown) {
    this.raw.set(value === undefined ? undefined : JSON.stringify(value));
  }

  private readonly parsed = computed<unknown | undefined>(() => {
    const v = this.raw();
    if (v === null || v === undefined || v === '') return undefined;
    try {
      return JSON.parse(v);
    } catch {
      return v;
    }
  });

  readonly formatted = computed<string | undefined>(() => {
    const p = this.parsed();
    if (p === undefined) return undefined;
    return typeof p === 'string' ? p : JSON.stringify(p, null, 2);
  });

  readonly preview = computed<string>(() => {
    const f = this.formatted() ?? '';
    const oneLine = f.replace(/\s+/g, ' ').trim();
    return oneLine.length > 60 ? oneLine.slice(0, 60) + '…' : oneLine || '{}';
  });
}
