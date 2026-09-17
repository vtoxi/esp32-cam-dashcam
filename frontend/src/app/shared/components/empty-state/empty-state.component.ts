import { Component, Input } from '@angular/core';
import { IconComponent } from '../icon/icon.component';

@Component({
  selector: 'app-empty-state',
  standalone: true,
  imports: [IconComponent],
  template: `
    <div class="flex flex-col items-center justify-center gap-2 py-16 px-6 text-center">
      <app-icon [name]="icon" class="w-10 h-10 text-slate-600 mb-1" [strokeWidth]="1.4" />
      <p class="text-slate-300 font-medium">{{ message }}</p>
      @if (hint) {
        <p class="text-sm text-slate-500 max-w-sm">{{ hint }}</p>
      }
    </div>
  `,
})
export class EmptyStateComponent {
  @Input() icon = 'inbox';
  @Input() message = 'Nothing here yet.';
  @Input() hint = '';
}
