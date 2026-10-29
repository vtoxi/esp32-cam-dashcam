import { Component, Input } from '@angular/core';
import { IconComponent } from '../icon/icon.component';

/** Consistent title/subtitle/actions band at the top of every feature page. */
@Component({
  selector: 'app-page-header',
  standalone: true,
  imports: [IconComponent],
  template: `
    <div class="flex items-start justify-between gap-4 flex-wrap mb-6">
      <div class="flex items-center gap-3.5">
        @if (icon) {
          <div class="flex items-center justify-center w-11 h-11 rounded-xl bg-brand-500/15 text-brand-400 shrink-0">
            <app-icon [name]="icon" class="w-6 h-6" />
          </div>
        }
        <div>
          <h1 class="text-xl font-bold text-white tracking-tight">{{ title }}</h1>
          @if (subtitle) {
            <p class="text-sm text-slate-400 mt-0.5">{{ subtitle }}</p>
          }
        </div>
      </div>
      <div class="flex items-center gap-2 flex-wrap">
        <ng-content></ng-content>
      </div>
    </div>
  `,
})
export class PageHeaderComponent {
  @Input() title = '';
  @Input() subtitle = '';
  @Input() icon = '';
}
