import { Component, Input, signal } from '@angular/core';
import { IconComponent } from '../icon/icon.component';

/** A monospace value with a copy-to-clipboard button — used for device
 * credentials, webhook secrets, and IDs shown exactly once. */
@Component({
  selector: 'app-copy-field',
  standalone: true,
  imports: [IconComponent],
  template: `
    <div class="inline-flex items-center gap-1 max-w-full rounded-lg bg-surface-200/70 border border-white/5 pl-3 pr-1 py-1">
      <code class="text-xs text-brand-200 overflow-x-auto whitespace-nowrap max-w-[320px]">{{ value }}</code>
      <button
        type="button"
        class="focus-ring flex items-center justify-center w-7 h-7 rounded-md text-slate-400 hover:text-white hover:bg-white/10 transition-colors shrink-0"
        (click)="copy()"
        [attr.aria-label]="copied() ? 'Copied' : 'Copy to clipboard'"
        [title]="copied() ? 'Copied' : 'Copy to clipboard'"
      >
        <app-icon [name]="copied() ? 'check' : 'clipboard-document'" class="w-4 h-4" />
      </button>
    </div>
  `,
})
export class CopyFieldComponent {
  @Input({ required: true }) value = '';
  readonly copied = signal(false);

  async copy(): Promise<void> {
    try {
      await navigator.clipboard.writeText(this.value);
      this.copied.set(true);
      setTimeout(() => this.copied.set(false), 1500);
    } catch {
      /* clipboard API unavailable (insecure context) — nothing to fall back to gracefully */
    }
  }
}
