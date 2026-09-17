import { Component, Input } from '@angular/core';
import { IconComponent } from '../icon/icon.component';

export type StatCardTone = 'primary' | 'success' | 'warning' | 'danger' | 'neutral';

const ICON_WRAP: Record<StatCardTone, string> = {
  primary: 'bg-brand-500/15 text-brand-400',
  success: 'bg-emerald-400/15 text-emerald-400',
  warning: 'bg-amber-400/15 text-amber-400',
  danger: 'bg-rose-400/15 text-rose-400',
  neutral: 'bg-slate-500/15 text-slate-400',
};

/** Dashboard summary tile — device counts, online counts, incident totals. */
@Component({
  selector: 'app-stat-card',
  standalone: true,
  imports: [IconComponent],
  template: `
    <div class="flex items-center gap-4 rounded-2xl border border-white/5 bg-surface-400/60 p-5 shadow-panel min-w-[190px]">
      <div class="flex items-center justify-center w-12 h-12 rounded-xl shrink-0 {{ iconWrapClasses }}">
        <app-icon [name]="icon" class="w-6 h-6" />
      </div>
      <div class="flex flex-col">
        <span class="text-2xl font-bold leading-tight text-white tabular-nums">{{ value }}</span>
        <span class="text-sm text-slate-400">{{ label }}</span>
      </div>
    </div>
  `,
})
export class StatCardComponent {
  @Input() icon = 'signal';
  @Input() value: string | number = 0;
  @Input() label = '';
  @Input() tone: StatCardTone = 'primary';

  get iconWrapClasses(): string {
    return ICON_WRAP[this.tone];
  }
}
