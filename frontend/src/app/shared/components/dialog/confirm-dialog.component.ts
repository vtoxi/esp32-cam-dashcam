import { Component, Input } from '@angular/core';
import type { DialogRef } from './dialog.service';
import { DialogShellComponent } from './dialog-shell.component';

export interface ConfirmDialogData {
  title: string;
  message: string;
  confirmLabel?: string;
  cancelLabel?: string;
  danger?: boolean;
}

/** One reusable "are you sure?" dialog for every destructive/consequential
 * action (delete a webhook subscription, issue a remote command). */
@Component({
  selector: 'app-confirm-dialog',
  standalone: true,
  imports: [DialogShellComponent],
  template: `
    <app-dialog-shell [title]="data.title" (dismiss)="dialogRef.close(false)">
      <p class="text-sm text-slate-300">{{ data.message }}</p>
      <div class="flex items-center justify-end gap-2 mt-6">
        <button
          type="button"
          class="focus-ring px-4 py-2 rounded-lg text-sm font-medium text-slate-300 hover:bg-white/5"
          (click)="dialogRef.close(false)"
        >
          {{ data.cancelLabel ?? 'Cancel' }}
        </button>
        <button
          type="button"
          class="focus-ring px-4 py-2 rounded-lg text-sm font-semibold text-white transition-colors"
          [class]="
            data.danger
              ? 'bg-rose-600 hover:bg-rose-500'
              : 'bg-brand-600 hover:bg-brand-500'
          "
          (click)="dialogRef.close(true)"
        >
          {{ data.confirmLabel ?? 'Confirm' }}
        </button>
      </div>
    </app-dialog-shell>
  `,
})
export class ConfirmDialogComponent {
  @Input({ required: true }) data!: ConfirmDialogData;
  dialogRef!: DialogRef<boolean>;
}
